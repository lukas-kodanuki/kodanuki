#include "plugins/build/cut_long_programs/cut_long_programs.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

namespace kodanuki {

constexpr double keptShare        = 0.75;
constexpr std::size_t mostSteps   = 0xFFFF;
constexpr std::size_t recentLists = 8;
constexpr std::uint32_t unstored  = 0xFFFFFFFFu;

struct CarriedCut {
	std::uint32_t object;
	std::shared_ptr<const std::vector<std::uint16_t>> entries;
	std::uint32_t ranges;
	std::uint32_t rerouted;
	std::uint32_t kept;
	std::uint32_t result;
	math::Interval range;
	std::uint32_t id;
};

struct Shortcuts {
	std::vector<std::uint32_t> alias;
	std::vector<math::Interval> intervals;
	std::vector<std::uint32_t> read;
};

struct Cutting {
	const GrowContext& context;
	const Subtree& grown;
	Subtree& cut;
	std::vector<std::uint32_t>& stored;
	Shortcuts& scratch;
};

struct CellStep {
	const std::vector<CarriedCut>& carried;
	std::uint32_t cell;
	Box box;
};

double cutShare() {
	return keptShare;
}

std::size_t wholeCount(const Cutting& cutting, std::uint32_t object) {
	return cutting.context.scene.objectPrograms[object].steps.size();
}

bool cutsObject(const Cutting& cutting, std::uint32_t object) {
	return wholeCount(cutting, object) <= mostSteps;
}

KeptSteps carriedSteps(const Cutting& cutting, const CarriedCut& carried) {
	const Program& whole = cutting.context.scene.objectPrograms[carried.object];
	if (!carried.entries) {
		return wholeSteps(whole);
	}
	return KeptSteps {
		.program  = whole,
		.entries  = carried.entries->data(),
		.ranges   = carried.ranges,
		.rerouted = carried.rerouted,
		.result   = carried.result,
	};
}

Inputs<math::Interval> boxPoints(const Box& box) {
	const math::Interval zero = {
		.lower = 0,
		.upper = 0,
	};

	return Inputs<math::Interval> {
		.arguments = nullptr,
		.p         = { box.axes[0], box.axes[1], box.axes[2] },
		.n         = { zero, zero, zero },
	};
}

std::optional<std::uint32_t> apartMinimum(const Step& step, const std::vector<math::Interval>& intervals) {
	const math::Interval& a = intervals[step.a];
	const math::Interval& b = intervals[step.b];
	if (a.upper < b.lower) {
		return step.a;
	}
	if (b.upper < a.lower) {
		return step.b;
	}
	return std::nullopt;
}

std::optional<std::uint32_t> apartMaximum(const Step& step, const std::vector<math::Interval>& intervals) {
	const math::Interval& a = intervals[step.a];
	const math::Interval& b = intervals[step.b];
	if (a.lower > b.upper) {
		return step.a;
	}
	if (b.lower > a.upper) {
		return step.b;
	}
	return std::nullopt;
}

std::optional<std::uint32_t> settledSelect(const Step& step, const std::vector<math::Interval>& intervals) {
	const math::Interval& condition = intervals[step.a];
	if (condition.lower > 0 || condition.upper < 0) {
		return step.b;
	}
	if (condition.lower == 0 && condition.upper == 0) {
		return step.c;
	}
	return std::nullopt;
}

std::optional<std::uint32_t> decidedJoin(const Step& step, const std::vector<math::Interval>& intervals) {
	switch (step.opcode) {
		case Opcode::Minimum: return apartMinimum(step, intervals);
		case Opcode::Maximum: return apartMaximum(step, intervals);
		case Opcode::Select: return settledSelect(step, intervals);
		default: return std::nullopt;
	}
}

bool decidableJoin(Opcode opcode) {
	return opcode == Opcode::Minimum || opcode == Opcode::Maximum || opcode == Opcode::Select;
}

void fillTargets(const KeptSteps& steps, std::vector<std::uint32_t>& targets) {
	targets.assign(steps.program.steps.size(), unstored);
	if (steps.entries == nullptr) {
		std::iota(targets.begin(), targets.end(), 0u);
		return;
	}
	for (std::uint32_t r = 0; r < steps.ranges; ++r) {
		std::iota(targets.begin() + steps.entries[2 * r], targets.begin() + steps.entries[2 * r + 1], std::uint32_t(steps.entries[2 * r]));
	}
	const std::uint16_t* pairs = steps.entries + 2 * steps.ranges;
	for (std::uint32_t k = 0; k < steps.rerouted; ++k) {
		targets[pairs[2 * k]] = pairs[2 * k + 1];
	}
}

void fillShortcuts(const KeptSteps& steps, const Box& box, Shortcuts& shortcuts) {
	fillTargets(steps, shortcuts.alias);
	evaluateKept(steps, boxPoints(box), shortcuts.intervals);
	for (std::uint32_t i = 0; i < shortcuts.alias.size(); ++i) {
		const std::uint32_t target = shortcuts.alias[i];
		if (target == unstored) {
			continue;
		}
		if (target != i) {
			shortcuts.alias[i] = shortcuts.alias[target];
			continue;
		}
		if (!decidableJoin(steps.program.steps[i].opcode)) {
			continue;
		}
		const std::optional<std::uint32_t> side = decidedJoin(renumberStep(steps.program.steps[i], shortcuts.alias), shortcuts.intervals);
		shortcuts.alias[i]                      = side.value_or(i);
	}
}

std::uint32_t fillReads(const Program& program, Shortcuts& shortcuts, std::uint32_t result) {
	std::vector<std::uint32_t>& read = shortcuts.read;
	read.assign(program.steps.size(), unstored);
	read[result] = result;
	for (std::size_t i = program.steps.size(); i-- > 0;) {
		if (read[i] != i) {
			continue;
		}
		const Step& step                = program.steps[i];
		const int count                 = operandCount(step.opcode);
		const std::uint32_t operands[3] = { step.a, step.b, step.c };
		for (int k = 0; k < count; ++k) {
			const std::uint32_t target = shortcuts.alias[operands[k]];
			read[operands[k]]          = target;
			read[target]               = target;
		}
	}
	std::uint32_t kept = 0;
	for (std::uint32_t i = 0; i < read.size(); ++i) {
		kept += read[i] == i ? 1 : 0;
	}
	return kept;
}

CarriedCut cutFromReads(std::uint32_t object, const std::vector<std::uint32_t>& read, std::uint32_t result) {
	std::vector<std::uint16_t> entries;

	CarriedCut cut = {
		.object   = object,
		.entries  = nullptr,
		.ranges   = 0,
		.rerouted = 0,
		.kept     = 0,
		.result   = result,
		.range    = {},
		.id       = unstored,
	};

	std::vector<std::uint16_t> pairs;
	for (std::uint32_t i = 0; i < read.size(); ++i) {
		const bool kept = read[i] == i;
		if (kept && (i == 0 || read[i - 1] != i - 1)) {
			entries.push_back(std::uint16_t(i));
		}
		if (kept && (i + 1 == read.size() || read[i + 1] != i + 1)) {
			entries.push_back(std::uint16_t(i + 1));
		}
		if (!kept && read[i] != unstored) {
			pairs.push_back(std::uint16_t(i));
			pairs.push_back(std::uint16_t(read[i]));
		}
		cut.kept += kept ? 1 : 0;
	}
	cut.ranges   = std::uint32_t(entries.size() / 2);
	cut.rerouted = std::uint32_t(pairs.size() / 2);
	entries.insert(entries.end(), pairs.begin(), pairs.end());
	cut.entries = std::make_shared<const std::vector<std::uint16_t>>(std::move(entries));
	return cut;
}

std::size_t keptCount(const Cutting& cutting, const CarriedCut& cut) {
	return !cut.entries ? wholeCount(cutting, cut.object) : cut.kept;
}

CarriedCut acceptedCut(const Cutting& cutting, const CarriedCut& reference, const Box& box) {
	if (!cutsObject(cutting, reference.object)) {
		return reference;
	}
	const KeptSteps steps = carriedSteps(cutting, reference);
	Shortcuts& shortcuts  = cutting.scratch;
	fillShortcuts(steps, box, shortcuts);
	const std::uint32_t result = shortcuts.alias[steps.result];
	const std::uint32_t kept   = fillReads(steps.program, shortcuts, result);
	if (double(kept) > keptShare * double(keptCount(cutting, reference))) {
		CarriedCut parent = reference;
		parent.range      = shortcuts.intervals[steps.result];
		return parent;
	}
	CarriedCut own = cutFromReads(reference.object, shortcuts.read, result);
	own.range      = shortcuts.intervals[steps.result];
	own.id         = std::uint32_t(cutting.stored.size());
	cutting.stored.push_back(unstored);
	return own;
}

CarriedCut wholeCut(std::uint32_t object) {
	const math::Interval everywhere = {
		.lower = -std::numeric_limits<double>::infinity(),
		.upper = std::numeric_limits<double>::infinity(),
	};

	return CarriedCut {
		.object   = object,
		.entries  = nullptr,
		.ranges   = 0,
		.rerouted = 0,
		.kept     = 0,
		.result   = 0,
		.range    = everywhere,
		.id       = unstored,
	};
}

std::vector<CarriedCut> carriedBelow(const Cutting& cutting, const std::vector<CarriedCut>& carried, const Box& box) {
	std::vector<CarriedCut> cuts;
	double lowest = std::numeric_limits<double>::infinity();
	for (const CarriedCut& reference : carried) {
		cuts.push_back(acceptedCut(cutting, reference, box));
		lowest = std::min(lowest, cuts.back().range.upper);
	}
	std::vector<CarriedCut> deciding;
	for (CarriedCut& cut : cuts) {
		if (!(cut.range.lower > 0) && !(cut.range.lower > lowest)) {
			deciding.push_back(std::move(cut));
		}
	}
	return deciding;
}

std::uint32_t storedCut(const Cutting& cutting, const CarriedCut& carried) {
	if (cutting.stored[carried.id] != unstored) {
		return cutting.stored[carried.id];
	}

	const ProgramCut cut = {
		.object   = carried.object,
		.first    = std::uint32_t(cutting.cut.cutSteps.size()),
		.ranges   = std::uint16_t(carried.ranges),
		.rerouted = std::uint16_t(carried.rerouted),
		.result   = carried.result,
	};

	cutting.cut.cutSteps.insert(cutting.cut.cutSteps.end(), carried.entries->begin(), carried.entries->end());
	cutting.cut.cuts.push_back(cut);
	cutting.stored[carried.id] = std::uint32_t(cutting.cut.cuts.size() - 1);
	return cutting.stored[carried.id];
}

ProgramReference cutReference(const Cutting& cutting, const std::vector<CarriedCut>& carried, std::uint32_t object) {
	const ProgramReference whole = {
		.index  = object,
		.source = ProgramSource::Object,
	};

	const auto found = std::find_if(carried.begin(), carried.end(), [object](const CarriedCut& cut) {
		return cut.object == object;
	});

	if (found == carried.end() || !found->entries) {
		return whole;
	}
	return ProgramReference {
		.index  = storedCut(cutting, *found),
		.source = ProgramSource::Cut,
	};
}

bool sameReferences(const Subtree& subtree, const ProgramList& list, const std::vector<ProgramReference>& references) {
	if (list.count != references.size()) {
		return false;
	}
	for (std::uint32_t k = 0; k < list.count; ++k) {
		const ProgramReference& held = subtree.references[list.first + k];
		if (held.index != references[k].index || held.source != references[k].source) {
			return false;
		}
	}
	return true;
}

std::uint32_t writtenList(Subtree& subtree, const std::vector<ProgramReference>& references) {
	const std::size_t count = subtree.lists.size();
	for (std::size_t back = 1; back <= std::min(count, recentLists); ++back) {
		if (sameReferences(subtree, subtree.lists[count - back], references)) {
			return std::uint32_t(count - back);
		}
	}

	const ProgramList list = {
		.first = std::uint32_t(subtree.references.size()),
		.count = std::uint32_t(references.size()),
	};

	subtree.references.insert(subtree.references.end(), references.begin(), references.end());
	subtree.lists.push_back(list);
	return std::uint32_t(count);
}

std::uint32_t cutList(const Cutting& cutting, const CellStep& at, std::uint32_t list) {
	const ProgramList& objects = cutting.grown.lists[list];
	std::vector<ProgramReference> references;
	for (std::uint32_t k = 0; k < objects.count; ++k) {
		references.push_back(cutReference(cutting, at.carried, cutting.grown.references[objects.first + k].index));
	}
	return writtenList(cutting.cut, references);
}

void cutBelow(const Cutting& cutting, const CellStep& at) {
	const CellEntry entry = cutting.grown.cells[at.cell];
	if (entry.kind == CellKind::Empty) {
		return;
	}
	const std::vector<CarriedCut> carried = carriedBelow(cutting, at.carried, at.box);

	const CellStep here = {
		.carried = carried,
		.cell    = at.cell,
		.box     = at.box,
	};

	switch (entry.kind) {
		case CellKind::Surface: cutting.cut.surfaces[entry.payload].programs = cutList(cutting, here, cutting.grown.surfaces[entry.payload].programs); return;
		case CellKind::Filled: cutting.cut.solids[entry.payload].programs = cutList(cutting, here, cutting.grown.solids[entry.payload].programs); return;
		case CellKind::Branch: break;
		case CellKind::Empty: return;
	}

	for (int octant = 0; octant < 8; ++octant) {
		const CellStep child = {
			.carried = carried,
			.cell    = entry.payload + std::uint32_t(octant),
			.box     = octantBox(at.box, octant),
		};

		cutBelow(cutting, child);
	}
}

std::vector<CarriedCut> wholeCuts(const CellPrograms& programs) {
	std::vector<CarriedCut> whole;
	for (const std::uint32_t object : programs.objects) {
		whole.push_back(wholeCut(object));
	}
	return whole;
}

Subtree cutLongPrograms(const GrowContext& context, const Subtree& subtree, const CellPrograms& programs) {
	Subtree cut    = subtree;
	cut.lists      = {};
	cut.references = {};
	cut.cuts       = {};
	cut.cutSteps   = {};

	std::vector<std::uint32_t> stored;

	Shortcuts scratch = {
		.alias     = {},
		.intervals = {},
		.read      = {},
	};

	const Cutting cutting = {
		.context = context,
		.grown   = subtree,
		.cut     = cut,
		.stored  = stored,
		.scratch = scratch,
	};

	const std::vector<CarriedCut> whole = wholeCuts(programs);

	const CellStep root = {
		.carried = whole,
		.cell    = 0,
		.box     = subtree.box,
	};

	cutBelow(cutting, root);
	return cut;
}

}
