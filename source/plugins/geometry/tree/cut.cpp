#include "plugins/geometry/tree/cut.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

namespace kodanuki {

constexpr std::uint32_t noOperand = 0xFFFFFFFFu;

struct Rewrite {
	std::vector<Step> steps;
	std::vector<std::uint32_t>& alias;
};

struct Writing {
	Step* steps;
	std::uint32_t* alias;
	const math::Interval* intervals;
};

Inputs<math::Interval> boxInputs(const Box& box) {
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

std::vector<math::Interval> intervalsOver(const Program& program, const Box& box) {
	std::vector<math::Interval> registers;
	evaluate(program, boxInputs(box), registers);
	return registers;
}

std::vector<math::Interval> intervalsAfter(const Program& program, const Box& box, const KnownSteps& known) {
	std::vector<math::Interval> registers(known.intervals.begin(), known.intervals.begin() + known.count);
	resumedEvaluation(program, boxInputs(box), registers);
	return registers;
}

bool isPoint(const math::Interval& interval) {
	return interval.lower == interval.upper;
}

bool isLiteral(Opcode opcode) {
	return opcode == Opcode::Constant || opcode == Opcode::Argument;
}

std::uint32_t separated(const Step& step, const math::Interval* intervals, bool lowerWins) {
	const math::Interval& a = intervals[step.a];
	const math::Interval& b = intervals[step.b];
	if (a.upper <= b.lower) {
		return lowerWins ? step.a : step.b;
	}
	if (b.upper <= a.lower) {
		return lowerWins ? step.b : step.a;
	}
	return noOperand;
}

std::uint32_t outsideBlend(const Step& step, const math::Interval* intervals) {
	const double radius    = intervals[step.c].lower;
	const math::Interval d = intervals[step.a] - intervals[step.b];
	if (d.upper <= -radius) {
		return step.a;
	}
	if (d.lower >= radius) {
		return step.b;
	}
	return noOperand;
}

std::uint32_t decidedSelect(const Step& step, const math::Interval* intervals) {
	const math::Interval& condition = intervals[step.a];
	if (condition.lower > 0 || condition.upper < 0) {
		return step.b;
	}
	if (condition.lower == 0 && condition.upper == 0) {
		return step.c;
	}
	return noOperand;
}

std::uint32_t decidedAbsolute(const Step& step, const math::Interval* intervals) {
	if (intervals[step.a].lower >= 0) {
		return step.a;
	}
	return noOperand;
}

bool isValue(const math::Interval& interval, double value) {
	return interval.lower == value && interval.upper == value;
}

std::uint32_t neutralOperand(const Step& step, const math::Interval* intervals, double neutral) {
	const bool commutes = step.opcode == Opcode::Add || step.opcode == Opcode::Multiply;
	if (isValue(intervals[step.b], neutral)) {
		return step.a;
	}
	if (commutes && isValue(intervals[step.a], neutral)) {
		return step.b;
	}
	return noOperand;
}

std::uint32_t shortcut(const Step& step, const math::Interval* intervals) {
	switch (step.opcode) {
		case Opcode::Add:
		case Opcode::Subtract: return neutralOperand(step, intervals, 0);
		case Opcode::Multiply:
		case Opcode::Divide: return neutralOperand(step, intervals, 1);
		case Opcode::Minimum: return separated(step, intervals, true);
		case Opcode::Maximum: return separated(step, intervals, false);
		case Opcode::SmoothMinimum: return outsideBlend(step, intervals);
		case Opcode::Absolute: return decidedAbsolute(step, intervals);
		case Opcode::Select: return decidedSelect(step, intervals);
		default: return noOperand;
	}
}

Step constantStep(const Step& step, double value) {
	return Step {
		.opcode = Opcode::Constant,
		.a      = 0,
		.b      = 0,
		.c      = 0,
		.value  = value,
		.leaf   = step.leaf,
	};
}

Step negatedStep(const Step& step, std::uint32_t operand) {
	Step negated   = step;
	negated.opcode = Opcode::Negate;
	negated.a      = operand;
	negated.b      = 0;
	negated.c      = 0;
	return negated;
}

std::uint32_t negatedOperand(const Step& step, const math::Interval* intervals) {
	const bool product = step.opcode == Opcode::Multiply;
	if (step.opcode == Opcode::Absolute && intervals[step.a].upper <= 0) {
		return step.a;
	}
	if (step.opcode == Opcode::Subtract && isValue(intervals[step.a], 0)) {
		return step.b;
	}
	if (product && isValue(intervals[step.a], -1)) {
		return step.b;
	}
	if (product && isValue(intervals[step.b], -1)) {
		return step.a;
	}
	return noOperand;
}

Step resolvedStep(const Step& step, const math::Interval* intervals, std::uint32_t index) {
	if (isPoint(intervals[index]) && !isLiteral(step.opcode)) {
		return constantStep(step, intervals[index].lower);
	}
	if (step.opcode == Opcode::PositiveSquare && intervals[step.a].lower >= 0) {
		Step square   = step;
		square.opcode = Opcode::Multiply;
		square.b      = step.a;
		return square;
	}
	const std::uint32_t negated = negatedOperand(step, intervals);
	if (negated != noOperand) {
		return negatedStep(step, negated);
	}
	return step;
}

Step unsquared(const Step* steps, const Step& step) {
	if (step.opcode != Opcode::SquareRoot) {
		return step;
	}
	const Step& inner = steps[step.a];
	if (inner.opcode == Opcode::PositiveSquare) {
		Step positive   = inner;
		positive.opcode = Opcode::Maximum;
		return positive;
	}
	if (inner.opcode != Opcode::Multiply || inner.a != inner.b) {
		return step;
	}
	Step absolute   = step;
	absolute.opcode = Opcode::Absolute;
	absolute.a      = inner.a;
	return absolute;
}

bool isZeroStep(const Step* steps, std::uint32_t index) {
	const Step& step = steps[index];
	return step.opcode == Opcode::Constant && step.value == 0;
}

Step positiveSquared(const Step* steps, const Step& step) {
	if (step.opcode != Opcode::Multiply || step.a != step.b) {
		return step;
	}
	const Step& inner = steps[step.a];
	if (inner.opcode != Opcode::Maximum || !(isZeroStep(steps, inner.a) || isZeroStep(steps, inner.b))) {
		return step;
	}
	const bool zeroFirst = isZeroStep(steps, inner.a);
	Step square          = step;
	square.opcode        = Opcode::PositiveSquare;
	square.a             = zeroFirst ? inner.b : inner.a;
	square.b             = zeroFirst ? inner.a : inner.b;
	return square;
}

Step aliasedOperands(const Step& original, const std::uint32_t* alias) {
	const int count = operandCount(original.opcode);
	Step step       = original;
	step.a          = count > 0 ? alias[original.a] : original.a;
	step.b          = count > 1 ? alias[original.b] : 0;
	step.c          = count > 2 ? alias[original.c] : 0;
	return step;
}

Step literalStep(const Step& original) {
	Step literal = original;
	literal.b    = 0;
	literal.c    = 0;
	return literal;
}

void rewriteStep(const Writing& writing, const Step& original, std::uint32_t index) {
	if (isLiteral(original.opcode)) {
		writing.alias[index] = index;
		writing.steps[index] = literalStep(original);
		return;
	}
	const math::Interval* intervals = writing.intervals;
	const Step step                 = positiveSquared(writing.steps, unsquared(writing.steps, aliasedOperands(original, writing.alias)));
	const std::uint32_t other       = isPoint(intervals[index]) ? noOperand : shortcut(step, intervals);
	writing.alias[index]            = other != noOperand ? other : index;
	writing.steps[index]            = resolvedStep(step, intervals, index);
}

Occupancy occupancyOf(const math::Interval& range) {
	if (range.lower > 0) {
		return Occupancy::Empty;
	}
	if (range.upper < 0) {
		return Occupancy::Filled;
	}
	return Occupancy::Surface;
}

std::vector<std::uint32_t>& rewriteAlias() {
	thread_local std::vector<std::uint32_t> alias;
	return alias;
}

std::vector<math::Interval>& cellIntervals() {
	thread_local std::vector<math::Interval> intervals;
	return intervals;
}

void startedRewrite(Rewrite& rewrite, const KnownRewrite& known) {
	rewrite.steps.assign(known.steps, known.steps + known.count);
	if (known.alias != nullptr) {
		rewrite.alias.assign(known.alias, known.alias + known.count);
		return;
	}
	rewrite.alias.resize(known.count);
	std::iota(rewrite.alias.begin(), rewrite.alias.end(), 0u);
}

void rewriteFrom(Rewrite& rewrite, const Program& program, const CutFrom& from) {
	rewrite.steps.clear();
	rewrite.alias.clear();
	rewrite.steps.reserve(program.steps.size());
	rewrite.alias.reserve(program.steps.size());
	startedRewrite(rewrite, from.known);
	rewrite.steps.resize(program.steps.size());
	rewrite.alias.resize(program.steps.size());

	const Writing writing = {
		.steps     = rewrite.steps.data(),
		.alias     = rewrite.alias.data(),
		.intervals = from.intervals.data(),
	};

	for (std::uint32_t i = from.known.count; i < program.steps.size(); ++i) {
		rewriteStep(writing, program.steps[i], i);
	}
}

KnownRewrite noRewrite() {
	return KnownRewrite {
		.steps = nullptr,
		.alias = nullptr,
		.count = 0,
	};
}

void rewriteInto(const Program& program, const std::vector<math::Interval>& intervals, Rewritten& rewritten) {
	Rewrite rewrite = {
		.steps = std::move(rewritten.steps),
		.alias = rewritten.alias,
	};

	const CutFrom from = {
		.intervals = intervals,
		.known     = noRewrite(),
	};

	rewriteFrom(rewrite, program, from);
	rewritten.steps = std::move(rewrite.steps);
}

Cut cutWith(const Program& program, const CutFrom& from, std::vector<Step> storage) {
	Rewrite rewrite = {
		.steps = std::move(storage),
		.alias = rewriteAlias(),
	};

	rewriteFrom(rewrite, program, from);

	Program rewritten = {
		.name      = program.name,
		.steps     = std::move(rewrite.steps),
		.result    = rewrite.alias[program.result],
		.arguments = program.arguments,
	};

	return Cut {
		.program   = prunedCompact(std::move(rewritten)),
		.range     = from.intervals[program.result],
		.occupancy = occupancyOf(from.intervals[program.result]),
	};
}

CutFrom wholeCutFrom(const std::vector<math::Interval>& intervals) {
	return CutFrom {
		.intervals = intervals,
		.known     = noRewrite(),
	};
}

Cut cutProgram(const Program& program, const Box& box) {
	const std::vector<math::Interval> intervals = intervalsOver(program, box);
	return cutWith(program, wholeCutFrom(intervals), {});
}

Cut occupiedCutBy(const Program& program, const std::vector<math::Interval>& intervals) {
	const math::Interval& range = intervals[program.result];
	if (occupancyOf(range) != Occupancy::Empty) {
		return cutWith(program, wholeCutFrom(intervals), {});
	}
	return Cut {
		.program   = {},
		.range     = range,
		.occupancy = Occupancy::Empty,
	};
}

Cut occupiedCutInto(const Program& program, const CutFrom& from, std::vector<Step> storage) {
	const math::Interval& range = from.intervals[program.result];
	if (occupancyOf(range) != Occupancy::Empty) {
		return cutWith(program, from, std::move(storage));
	}
	return occupiedCutBy(program, from.intervals);
}

Cut occupiedCut(const Program& program, const Box& box) {
	return occupiedCutBy(program, intervalsOver(program, box));
}

std::shared_ptr<const Program> borrowedProgram(const Program& program) {
	return std::shared_ptr<const Program>(std::shared_ptr<const Program>(), &program);
}

CellPrograms rootPrograms(const Scene& scene) {
	CellPrograms root = {
		.objects  = {},
		.programs = {},
	};

	for (std::uint32_t i = 0; i < scene.objectPrograms.size(); ++i) {
		root.objects.push_back(i);
		root.programs.push_back(borrowedProgram(scene.objectPrograms[i]));
	}
	return root;
}

struct ObjectCut {
	Cut cut;
	std::shared_ptr<const Program> program;
};

ObjectCut objectCut(const std::shared_ptr<const Program>& program, const Box& box) {
	std::vector<math::Interval>& intervals = cellIntervals();
	evaluate(*program, boxInputs(box), intervals);

	ObjectCut cut = {
		.cut     = occupiedCutBy(*program, intervals),
		.program = program,
	};

	if (cut.cut.occupancy == Occupancy::Empty || sameProgram(cut.cut.program, *program)) {
		cut.cut.program = {};
		return cut;
	}
	cut.program     = std::make_shared<const Program>(std::move(cut.cut.program));
	cut.cut.program = {};
	return cut;
}

CellCut filledCut(std::uint32_t object, const std::shared_ptr<const Program>& program) {
	const CellPrograms filling = {
		.objects  = { object },
		.programs = { program },
	};

	return CellCut {
		.occupancy = Occupancy::Filled,
		.programs  = filling,
	};
}

double lowestUpper(const std::vector<ObjectCut>& cuts) {
	double lowest = std::numeric_limits<double>::infinity();
	for (const ObjectCut& cut : cuts) {
		lowest = std::min(lowest, cut.cut.range.upper);
	}
	return lowest;
}

CellPrograms deciding(const CellPrograms& programs, const std::vector<ObjectCut>& cuts) {
	const double lowest = lowestUpper(cuts);

	CellPrograms kept = {
		.objects  = {},
		.programs = {},
	};

	for (std::size_t i = 0; i < cuts.size(); ++i) {
		if (cuts[i].cut.occupancy == Occupancy::Surface && !(cuts[i].cut.range.lower > lowest)) {
			kept.objects.push_back(programs.objects[i]);
			kept.programs.push_back(cuts[i].program);
		}
	}
	return kept;
}

CellCut cutCell(const CellPrograms& programs, const Box& box) {
	std::vector<ObjectCut> cuts;
	for (std::size_t i = 0; i < programs.programs.size(); ++i) {
		cuts.push_back(objectCut(programs.programs[i], box));
		if (cuts.back().cut.occupancy == Occupancy::Filled) {
			return filledCut(programs.objects[i], cuts.back().program);
		}
	}
	const CellPrograms kept = deciding(programs, cuts);
	return CellCut {
		.occupancy = kept.programs.empty() ? Occupancy::Empty : Occupancy::Surface,
		.programs  = kept,
	};
}

math::Interval halfSpan(const math::Interval& span, int side) {
	const double middle = (span.lower + span.upper) / 2;
	return math::Interval {
		.lower = side == 0 ? span.lower : middle,
		.upper = side == 0 ? middle : span.upper,
	};
}

Box octantBox(const Box& box, int octant) {
	return Box {
		.axes = { halfSpan(box.axes[0], octant & 1), halfSpan(box.axes[1], (octant >> 1) & 1), halfSpan(box.axes[2], (octant >> 2) & 1) },
	};
}

math::Interval regionSpan(const Region& region, int axis) {
	return math::Interval {
		.lower = region.lower[axis],
		.upper = region.lower[axis] + region.size[axis],
	};
}

Box regionBox(const Region& region) {
	return Box {
		.axes = { regionSpan(region, 0), regionSpan(region, 1), regionSpan(region, 2) },
	};
}

math::Interval placedSpan(const Region& region, const PlacedLeaf& leaf, int axis) {
	const double size  = std::ldexp(region.size[axis], -leaf.level);
	const double lower = region.lower[axis] + double(leaf.place[axis]) * size;
	return math::Interval {
		.lower = lower,
		.upper = lower + size,
	};
}

Box placedBox(const Region& region, const PlacedLeaf& leaf) {
	return Box {
		.axes = { placedSpan(region, leaf, 0), placedSpan(region, leaf, 1), placedSpan(region, leaf, 2) },
	};
}

double footprintAt(const View& view, const math::Vector& p) {
	const math::Vector towards = p - view.eye;
	const double distance      = std::hypot(towards.x, towards.y, towards.z);
	if (distance <= 0) {
		return 0;
	}
	const double cosine    = math::dot(towards, view.forward) / distance;
	const double narrowing = cosine > 0 ? std::max(cosine * cosine, view.corner) : view.corner;
	return distance * view.spread * narrowing;
}

double cellProgramsField(const CellPrograms& programs, const math::Vector& p) {
	const Inputs<double> inputs = {
		.arguments = nullptr,
		.p         = { p.x, p.y, p.z },
		.n         = { 0, 0, 0 },
	};

	thread_local std::vector<double> registers;
	double field = std::numeric_limits<double>::infinity();
	for (const std::shared_ptr<const Program>& program : programs.programs) {
		field = std::min(field, evaluate(*program, inputs, registers));
	}
	return field;
}

double cellField(const std::vector<Program>& programs, const math::Vector& p) {
	const Inputs<double> inputs = {
		.arguments = nullptr,
		.p         = { p.x, p.y, p.z },
		.n         = { 0, 0, 0 },
	};

	thread_local std::vector<double> registers;
	double field = std::numeric_limits<double>::infinity();
	for (const Program& program : programs) {
		field = std::min(field, evaluate(program, inputs, registers));
	}
	return field;
}

}
