#include "extras/detail_correct_split_hidden_primitives/split_hidden_primitives.h"
#include "tree/patch.h"
#include "tree/quadric.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace kodanuki {

constexpr double inwardShare = 1.0 / 16.0;

using Point = std::array<double, 3>;

HiddenPrimitives noHiddenPrimitives() {
	return HiddenPrimitives {
		.leaves = nullptr,
		.places = {},
	};
}

HiddenPrimitives hiddenPrimitives(const Scene& scene) {
	HiddenPrimitives hidden = noHiddenPrimitives();
	hidden.leaves           = std::make_shared<LeafPrograms>(scene, std::vector<std::atomic<std::shared_ptr<const Program>>>(scene.leaves.size()));
	for (std::uint32_t leaf = 0; leaf < scene.leaves.size(); ++leaf) {
		const double* origin = scene.leaves[leaf].placement.origin;
		const Point place    = { origin[0], origin[1], origin[2] };
		hidden.places.push_back(place);
	}
	return hidden;
}

bool readsLeaf(const Step& step) {
	switch (step.opcode) {
		case Opcode::Constant:
		case Opcode::Argument:
		case Opcode::PointX:
		case Opcode::PointY:
		case Opcode::PointZ: return false;
		default: return step.leaf != noLeaf;
	}
}

std::vector<std::uint32_t> cellLeaves(const CellPrograms& programs) {
	std::vector<std::uint32_t> leaves;
	for (const std::shared_ptr<const Program>& program : programs.programs) {
		for (const Step& step : program->steps) {
			if (readsLeaf(step)) {
				leaves.push_back(step.leaf);
			}
		}
	}
	std::sort(leaves.begin(), leaves.end());
	leaves.erase(std::unique(leaves.begin(), leaves.end()), leaves.end());
	return leaves;
}

bool fittedWhole(const Plan& plan) {
	return plan.kind == CellKind::Surface && (plan.patched || plan.seamed);
}

bool bothSigns(const Program& program, const Box& box) {
	const Readings readings = patchReadings(box);
	thread_local std::vector<double> registers;
	bool below = false;
	bool above = false;
	for (std::size_t k = 0; k < 35; ++k) {
		const double* p = readings.points[k];

		const Inputs<double> inputs = {
			.arguments = nullptr,
			.p         = { p[0], p[1], p[2] },
			.n         = { 0, 0, 0 },
		};

		const double value = evaluate(program, inputs, registers);
		below              = below || value <= 0;
		above              = above || value >= 0;
	}
	return below && above;
}

Point standingPoint(const Box& box, const Point& place) {
	Point near         = {};
	double smallest    = std::numeric_limits<double>::infinity();
	const Point centre = { boxCentre(box, 0), boxCentre(box, 1), boxCentre(box, 2) };
	Point towards      = {};
	for (int axis = 0; axis < 3; ++axis) {
		const std::size_t k = std::size_t(axis);
		near[k]             = std::clamp(place[k], box.axes[axis].lower, box.axes[axis].upper);
		towards[k]          = centre[k] - near[k];
		smallest            = std::min(smallest, 2 * boxHalf(box, axis));
	}
	const double apart = std::hypot(towards[0], towards[1], towards[2]);
	if (!(apart > 0)) {
		return near;
	}
	const double step = std::min(inwardShare * smallest, apart) / apart;
	return { near[0] + towards[0] * step, near[1] + towards[1] * step, near[2] + towards[2] * step };
}

double fitStrayAt(const HiddenQuery& query, const Point& p) {
	const Box& box           = query.box;
	const double u[3]        = { (p[0] - box.axes[0].lower) / (2 * boxHalf(box, 0)), (p[1] - box.axes[1].lower) / (2 * boxHalf(box, 1)), (p[2] - box.axes[2].lower) / (2 * boxHalf(box, 2)) };
	const math::Vector point = { p[0], p[1], p[2] };
	return std::abs(cellProgramsField(query.plan.programs, point) - fittedRunValue(query.plan.run.data(), u));
}

std::shared_ptr<const Program> askedLeaf(LeafPrograms& leaves, std::uint32_t leaf) {
	std::atomic<std::shared_ptr<const Program>>& slot = leaves.programs[leaf];
	std::shared_ptr<const Program> known              = slot.load();
	if (known != nullptr) {
		return known;
	}
	const std::shared_ptr<const Program> made = std::make_shared<const Program>(leafProgram(leaves.scene, leaf));
	return slot.compare_exchange_strong(known, made) ? made : known;
}

double leafStray(const HiddenPrimitives& hidden, const HiddenQuery& query, std::uint32_t leaf) {
	const std::shared_ptr<const Program> asked = askedLeaf(*hidden.leaves, leaf);
	const Program& program                     = *asked;
	const math::Interval range                 = intervalsOver(program, query.box)[program.result];
	if (range.lower > 0 || range.upper < 0) {
		return 0;
	}
	if (!bothSigns(program, query.box)) {
		return std::numeric_limits<double>::infinity();
	}
	return fitStrayAt(query, standingPoint(query.box, hidden.places[leaf]));
}

bool hidesLeaf(const HiddenPrimitives& hidden, const HiddenQuery& query) {
	if (!fittedWhole(query.plan) || hidden.places.empty()) {
		return false;
	}
	const std::vector<std::uint32_t> leaves = cellLeaves(query.plan.programs);
	if (leaves.size() < 2) {
		return false;
	}
	for (const std::uint32_t leaf : leaves) {
		if (leaf < hidden.places.size() && leafStray(hidden, query, leaf) > query.tolerance) {
			return true;
		}
	}
	return false;
}

}
