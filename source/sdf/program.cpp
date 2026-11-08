#include "sdf/program.h"
#include "sdf/numbers.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace kodanuki {

int operandCount(Opcode opcode) {
	switch (opcode) {
		case Opcode::Constant:
		case Opcode::Argument:
		case Opcode::PointX:
		case Opcode::PointY:
		case Opcode::PointZ:
		case Opcode::NormalX:
		case Opcode::NormalY:
		case Opcode::NormalZ: return 0;
		case Opcode::Negate:
		case Opcode::SquareRoot:
		case Opcode::Absolute:
		case Opcode::Floor:
		case Opcode::Sine:
		case Opcode::Cosine:
		case Opcode::Sign:
		case Opcode::Not: return 1;
		case Opcode::Select:
		case Opcode::SmoothMinimum: return 3;
		default: return 2;
	}
}

Step renumberStep(const Step& step, const std::vector<std::uint32_t>& map) {
	const int count = operandCount(step.opcode);
	Step renumbered = step;
	renumbered.a    = count > 0 ? map[step.a] : step.a;
	renumbered.b    = count > 1 ? map[step.b] : 0;
	renumbered.c    = count > 2 ? map[step.c] : 0;
	return renumbered;
}

template <typename T>
T compareStep(Opcode opcode, const T& a, const T& b) {
	switch (opcode) {
		case Opcode::Less: return math::compare(math::Compare::Less, a, b);
		case Opcode::LessEqual: return math::compare(math::Compare::LessEqual, a, b);
		case Opcode::Equal: return math::compare(math::Compare::Equal, a, b);
		case Opcode::NotEqual: return math::compare(math::Compare::NotEqual, a, b);
		case Opcode::And: return math::compare(math::Compare::And, a, b);
		case Opcode::Or: return math::compare(math::Compare::Or, a, b);
		default: return math::compare(math::Compare::Equal, a, math::constantAs<T>(0));
	}
}

template <typename T>
T runStep(const Step& step, const std::vector<T>& registers, const Inputs<T>& inputs) {
	const T& a = registers[step.a];
	const T& b = registers[step.b];
	switch (step.opcode) {
		case Opcode::Constant: return math::constantAs<T>(step.value);
		case Opcode::Argument: return math::constantAs<T>(inputs.arguments[step.a]);
		case Opcode::PointX: return inputs.p[0];
		case Opcode::PointY: return inputs.p[1];
		case Opcode::PointZ: return inputs.p[2];
		case Opcode::NormalX: return inputs.n[0];
		case Opcode::NormalY: return inputs.n[1];
		case Opcode::NormalZ: return inputs.n[2];
		case Opcode::Add: return a + b;
		case Opcode::Subtract: return a - b;
		case Opcode::Multiply: return a * b;
		case Opcode::Divide: return a / b;
		case Opcode::Negate: return -a;
		case Opcode::SquareRoot: return math::sqrt(a);
		case Opcode::Absolute: return math::abs(a);
		case Opcode::Floor: return math::floor(a);
		case Opcode::Sine: return math::sin(a);
		case Opcode::Cosine: return math::cos(a);
		case Opcode::Sign: return math::sign(a);
		case Opcode::ArcTangent: return math::atan2(a, b);
		case Opcode::Power: return math::pow(a, b);
		case Opcode::Minimum: return math::min(a, b);
		case Opcode::Maximum: return math::max(a, b);
		case Opcode::SmoothMinimum: return math::smoothMinimum(a, b, registers[step.c]);
		case Opcode::PositiveSquare: return math::positiveSquare(a);
		case Opcode::Select: return math::choose(a, b, registers[step.c]);
		default: return compareStep(step.opcode, a, b);
	}
}

template <typename T>
struct Evaluation {
	const std::vector<Step>& steps;
	const Inputs<T>& inputs;
	std::vector<T>& registers;
};

template <typename T>
void runRange(const Evaluation<T>& evaluation, std::size_t begin, std::size_t end) {
	for (std::size_t i = begin; i < end; ++i) {
		evaluation.registers[i] = runStep(evaluation.steps[i], evaluation.registers, evaluation.inputs);
	}
}

template <typename T>
T resumedEvaluation(const Program& program, const Inputs<T>& inputs, std::vector<T>& registers) {
	const std::size_t first = registers.size();
	registers.resize(program.steps.size(), math::constantAs<T>(0));

	const Evaluation<T> evaluation = {
		.steps     = program.steps,
		.inputs    = inputs,
		.registers = registers,
	};

	runRange(evaluation, first, program.steps.size());
	return registers[program.result];
}

template <typename T>
T evaluate(const Program& program, const Inputs<T>& inputs, std::vector<T>& registers) {
	registers.resize(program.steps.size(), math::constantAs<T>(0));

	const Evaluation<T> evaluation = {
		.steps     = program.steps,
		.inputs    = inputs,
		.registers = registers,
	};

	runRange(evaluation, 0, program.steps.size());
	return registers[program.result];
}

template double evaluate(const Program&, const Inputs<double>&, std::vector<double>&);
template math::Interval evaluate(const Program&, const Inputs<math::Interval>&, std::vector<math::Interval>&);
template math::Dual evaluate(const Program&, const Inputs<math::Dual>&, std::vector<math::Dual>&);
template math::HyperDual evaluate(const Program&, const Inputs<math::HyperDual>&, std::vector<math::HyperDual>&);
template math::IntervalHyperDual evaluate(const Program&, const Inputs<math::IntervalHyperDual>&, std::vector<math::IntervalHyperDual>&);
template math::Interval resumedEvaluation(const Program&, const Inputs<math::Interval>&, std::vector<math::Interval>&);

struct LaneEvaluation {
	const PointBatch& batch;
	std::vector<double>& registers;
};

template <typename Lane>
void eachLane(double* out, std::size_t count, const Lane& lane) {
	for (std::size_t k = 0; k < count; ++k) {
		out[k] = lane(k);
	}
}

const double* laneRow(const LaneEvaluation& evaluation, std::uint32_t step) {
	return evaluation.registers.data() + std::size_t(step) * evaluation.batch.count;
}

void pointLanes(const LaneEvaluation& evaluation, double* out, int axis) {
	eachLane(out, evaluation.batch.count, [&](std::size_t k) {
		return evaluation.batch.points[k][axis];
	});
}

double laneValue(const Step& step, double a, double b, double c) {
	switch (step.opcode) {
		case Opcode::Floor: return math::floor(a);
		case Opcode::Sine: return math::sin(a);
		case Opcode::Cosine: return math::cos(a);
		case Opcode::Sign: return math::sign(a);
		case Opcode::ArcTangent: return math::atan2(a, b);
		case Opcode::Power: return math::pow(a, b);
		case Opcode::SmoothMinimum: return math::smoothMinimum(a, b, c);
		case Opcode::Select: return math::choose(a, b, c);
		default: return compareStep(step.opcode, a, b);
	}
}

void scalarLanes(const Step& step, const LaneEvaluation& evaluation, double* out) {
	const int count = operandCount(step.opcode);
	const double* a = laneRow(evaluation, step.a);
	const double* b = laneRow(evaluation, count > 1 ? step.b : step.a);
	const double* c = laneRow(evaluation, count > 2 ? step.c : step.a);
	eachLane(out, evaluation.batch.count, [&](std::size_t k) {
		return laneValue(step, a[k], b[k], c[k]);
	});
}

bool isLaneBinary(Opcode opcode) {
	switch (opcode) {
		case Opcode::Add:
		case Opcode::Subtract:
		case Opcode::Multiply:
		case Opcode::Divide:
		case Opcode::Minimum:
		case Opcode::Maximum: return true;
		default: return false;
	}
}

double binaryLane(Opcode opcode, double a, double b) {
	switch (opcode) {
		case Opcode::Add: return a + b;
		case Opcode::Subtract: return a - b;
		case Opcode::Multiply: return a * b;
		case Opcode::Divide: return a / b;
		case Opcode::Minimum: return math::min(a, b);
		default: return math::max(a, b);
	}
}

bool isLaneUnary(Opcode opcode) {
	switch (opcode) {
		case Opcode::Negate:
		case Opcode::SquareRoot:
		case Opcode::Absolute:
		case Opcode::PositiveSquare: return true;
		default: return false;
	}
}

double unaryLane(Opcode opcode, double a) {
	switch (opcode) {
		case Opcode::Negate: return -a;
		case Opcode::SquareRoot: return math::sqrt(a);
		case Opcode::Absolute: return math::abs(a);
		default: return math::positiveSquare(a);
	}
}

void binaryLanes(const Step& step, const LaneEvaluation& evaluation, double* out) {
	const double* a = laneRow(evaluation, step.a);
	const double* b = laneRow(evaluation, step.b);
	eachLane(out, evaluation.batch.count, [&](std::size_t k) {
		return binaryLane(step.opcode, a[k], b[k]);
	});
}

void unaryLanes(const Step& step, const LaneEvaluation& evaluation, double* out) {
	const double* a = laneRow(evaluation, step.a);
	eachLane(out, evaluation.batch.count, [&](std::size_t k) {
		return unaryLane(step.opcode, a[k]);
	});
}

void runLanes(const Step& step, const LaneEvaluation& evaluation, std::uint32_t index) {
	double* out             = evaluation.registers.data() + std::size_t(index) * evaluation.batch.count;
	const std::size_t count = evaluation.batch.count;
	switch (step.opcode) {
		case Opcode::Constant: std::fill(out, out + count, math::constantAs<double>(step.value)); return;
		case Opcode::Argument: std::fill(out, out + count, evaluation.batch.arguments[step.a]); return;
		case Opcode::PointX: pointLanes(evaluation, out, 0); return;
		case Opcode::PointY: pointLanes(evaluation, out, 1); return;
		case Opcode::PointZ: pointLanes(evaluation, out, 2); return;
		case Opcode::NormalX:
		case Opcode::NormalY:
		case Opcode::NormalZ: std::fill(out, out + count, 0.0); return;
		default: break;
	}

	if (isLaneBinary(step.opcode)) {
		binaryLanes(step, evaluation, out);
		return;
	}
	if (isLaneUnary(step.opcode)) {
		unaryLanes(step, evaluation, out);
		return;
	}
	scalarLanes(step, evaluation, out);
}

const double* evaluatePoints(const Program& program, const PointBatch& batch, std::vector<double>& registers) {
	registers.resize(program.steps.size() * batch.count);

	const LaneEvaluation evaluation = {
		.batch     = batch,
		.registers = registers,
	};

	for (std::uint32_t i = 0; i < program.steps.size(); ++i) {
		runLanes(program.steps[i], evaluation, i);
	}
	return registers.data() + std::size_t(program.result) * batch.count;
}

KeptSteps wholeSteps(const Program& program) {
	return KeptSteps {
		.program  = program,
		.entries  = nullptr,
		.ranges   = 0,
		.rerouted = 0,
		.result   = program.result,
	};
}

template <typename T>
T evaluateKept(const KeptSteps& kept, const Inputs<T>& inputs, std::vector<T>& registers) {
	if (kept.entries == nullptr) {
		return evaluate(kept.program, inputs, registers);
	}
	if (registers.size() < kept.program.steps.size()) {
		registers.resize(kept.program.steps.size(), math::constantAs<T>(0));
	}

	const Evaluation<T> evaluation = {
		.steps     = kept.program.steps,
		.inputs    = inputs,
		.registers = registers,
	};

	const std::uint16_t* pairs = kept.entries + 2 * kept.ranges;
	const std::uint16_t* last  = pairs + 2 * kept.rerouted;
	for (std::uint32_t r = 0; r < kept.ranges; ++r) {
		const std::size_t begin = kept.entries[2 * r];
		for (; pairs < last && pairs[0] < begin; pairs += 2) {
			registers[pairs[0]] = registers[pairs[1]];
		}
		runRange(evaluation, begin, kept.entries[2 * r + 1]);
	}
	return registers[kept.result];
}

template double evaluateKept(const KeptSteps&, const Inputs<double>&, std::vector<double>&);
template math::Interval evaluateKept(const KeptSteps&, const Inputs<math::Interval>&, std::vector<math::Interval>&);
template math::Dual evaluateKept(const KeptSteps&, const Inputs<math::Dual>&, std::vector<math::Dual>&);
template math::Batch evaluateKept(const KeptSteps&, const Inputs<math::Batch>&, std::vector<math::Batch>&);
template math::DualBatch evaluateKept(const KeptSteps&, const Inputs<math::DualBatch>&, std::vector<math::DualBatch>&);

std::uint32_t reroutedStep(const KeptSteps& kept, std::uint32_t step) {
	const std::uint16_t* pairs = kept.entries + 2 * kept.ranges;
	for (std::uint32_t k = 0; k < kept.rerouted; ++k) {
		if (pairs[2 * k] == step) {
			return pairs[2 * k + 1];
		}
	}
	return step;
}

std::uint32_t aliasedOperand(std::uint32_t operand, std::uint32_t step, std::uint32_t target) {
	return operand == step ? target : operand;
}

Program aliasedStep(Program program, std::uint32_t step, std::uint32_t target) {
	for (std::size_t i = step + 1; i < program.steps.size(); ++i) {
		Step& moved     = program.steps[i];
		const int count = operandCount(moved.opcode);
		moved.a         = count > 0 ? aliasedOperand(moved.a, step, target) : moved.a;
		moved.b         = count > 1 ? aliasedOperand(moved.b, step, target) : 0;
		moved.c         = count > 2 ? aliasedOperand(moved.c, step, target) : 0;
	}
	program.result = aliasedOperand(program.result, step, target);
	return program;
}

std::vector<std::uint32_t>& pruneMarks() {
	thread_local std::vector<std::uint32_t> marks;
	return marks;
}

std::vector<std::uint32_t>& liveSteps(const Program& program) {
	std::vector<std::uint32_t>& live = pruneMarks();
	live.assign(program.steps.size(), 0);
	live[program.result] = 1;
	std::uint32_t* marks = live.data();
	const Step* steps    = program.steps.data();
	for (std::size_t i = program.steps.size(); i-- > 0;) {
		if (marks[i] == 0) {
			continue;
		}
		const Step step                 = steps[i];
		const int count                 = operandCount(step.opcode);
		const std::uint32_t operands[3] = { step.a, step.b, step.c };
		for (int k = 0; k < count; ++k) {
			marks[operands[k]] = 1;
		}
	}
	return live;
}

Program compactedProgram(Program program, std::vector<std::uint32_t>& map) {
	std::uint32_t kept      = 0;
	std::uint32_t* marks    = map.data();
	Step* steps             = program.steps.data();
	const std::size_t count = program.steps.size();
	for (std::size_t i = 0; i < count; ++i) {
		if (marks[i] != 0) {
			marks[i]        = kept;
			Step moved      = steps[i];
			const int reads = operandCount(moved.opcode);
			moved.a         = reads > 0 ? marks[moved.a] : moved.a;
			moved.b         = reads > 1 ? marks[moved.b] : 0;
			moved.c         = reads > 2 ? marks[moved.c] : 0;
			steps[kept]     = moved;
			kept += 1;
		}
	}
	program.steps.resize(kept);
	program.result = map[program.result];
	return program;
}

Program pruneProgram(Program program) {
	std::vector<std::uint32_t>& map = liveSteps(program);
	return compactedProgram(std::move(program), map);
}

bool allLive(const std::vector<std::uint32_t>& live, std::size_t count) {
	const auto end = live.begin() + std::ptrdiff_t(count);
	return std::find(live.begin(), end, 0u) == end;
}

Program prunedCompact(Program program) {
	std::vector<std::uint32_t>& map = liveSteps(program);
	if (allLive(map, program.steps.size())) {
		return program;
	}
	return compactedProgram(std::move(program), map);
}

bool sameStep(const Step& a, const Step& b) {
	return a.opcode == b.opcode && a.a == b.a && a.b == b.b && a.c == b.c && std::bit_cast<std::uint64_t>(a.value) == std::bit_cast<std::uint64_t>(b.value) && a.leaf == b.leaf;
}

bool sameProgram(const Program& a, const Program& b) {
	return a.result == b.result && a.arguments == b.arguments && std::equal(a.steps.begin(), a.steps.end(), b.steps.begin(), b.steps.end(), sameStep);
}

template <typename T>
struct LaneValues {
	const std::vector<T>& registers;
	int lane;
};

template <typename T>
double laneValueAt(const LaneValues<T>& values, std::uint32_t step) {
	return math::laneValue(values.registers[step], values.lane);
}

template <typename T>
std::uint32_t chosenOperand(const Step& step, const LaneValues<T>& values) {
	const double a = laneValueAt(values, step.a);
	const double b = laneValueAt(values, step.b);
	switch (step.opcode) {
		case Opcode::Minimum:
		case Opcode::SmoothMinimum: return a <= b ? step.a : step.b;
		case Opcode::Maximum: return a >= b ? step.a : step.b;
		case Opcode::Select: return a != 0 ? step.b : step.c;
		default: return step.a;
	}
}

bool isJoinStep(Opcode opcode) {
	switch (opcode) {
		case Opcode::Minimum:
		case Opcode::SmoothMinimum:
		case Opcode::Maximum:
		case Opcode::Negate:
		case Opcode::Select: return true;
		default: return false;
	}
}

template <typename T>
std::uint32_t leafFrom(const KeptSteps& kept, const LaneValues<T>& values, std::uint32_t step) {
	std::uint32_t at = step;
	while (isJoinStep(kept.program.steps[at].opcode)) {
		at = reroutedStep(kept, chosenOperand(kept.program.steps[at], values));
	}
	return kept.program.steps[at].leaf;
}

template <typename T>
double blendShare(const Step& step, const LaneValues<T>& values) {
	const double radius = laneValueAt(values, step.c);
	const double apart  = std::abs(laneValueAt(values, step.a) - laneValueAt(values, step.b));
	return radius > 0 && apart < radius ? 0.5 * (radius - apart) / radius : 0;
}

template <typename T>
Source laneSourceOf(const KeptSteps& kept, const LaneValues<T>& values) {
	Source source = {
		.leaf  = noLeaf,
		.other = noLeaf,
		.share = 0,
	};

	std::uint32_t at = kept.result;
	while (isJoinStep(kept.program.steps[at].opcode)) {
		const Step& step         = kept.program.steps[at];
		const std::uint32_t next = chosenOperand(step, values);
		const double share       = step.opcode == Opcode::SmoothMinimum ? blendShare(step, values) : 0;
		if (share > 0 && source.other == noLeaf) {
			source.other = leafFrom(kept, values, reroutedStep(kept, next == step.a ? step.b : step.a));
			source.share = share;
		}
		at = reroutedStep(kept, next);
	}
	source.leaf = kept.program.steps[at].leaf;
	return source;
}

template <typename T>
Source keptSourceOf(const KeptSteps& kept, const std::vector<T>& values) {
	const LaneValues<T> lane = {
		.registers = values,
		.lane      = 0,
	};

	return laneSourceOf(kept, lane);
}

template Source keptSourceOf(const KeptSteps&, const std::vector<double>&);
template Source keptSourceOf(const KeptSteps&, const std::vector<math::Dual>&);

Source keptBatchSourceOf(const KeptSteps& kept, const std::vector<math::DualBatch>& registers, int lane) {
	const LaneValues<math::DualBatch> values = {
		.registers = registers,
		.lane      = lane,
	};

	return laneSourceOf(kept, values);
}

Source sourceOf(const Program& program, const std::vector<double>& values) {
	return keptSourceOf(wholeSteps(program), values);
}

Source sourceAt(const Program& program, const math::Vector& p) {
	const Inputs<double> inputs = {
		.arguments = nullptr,
		.p         = { p.x, p.y, p.z },
		.n         = { 0, 0, 0 },
	};

	std::vector<double> values;
	evaluate(program, inputs, values);
	return sourceOf(program, values);
}

}
