#pragma once

#include "math/vector.h"
#include "sdf/numbers.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kodanuki {

template <typename T>
struct Inputs {
	const double* arguments;
	T p[3];
	T n[3];
};

enum class Opcode : std::uint8_t {
	Constant,
	Argument,
	PointX,
	PointY,
	PointZ,
	NormalX,
	NormalY,
	NormalZ,
	Add,
	Subtract,
	Multiply,
	Divide,
	Negate,
	SquareRoot,
	Absolute,
	Floor,
	Sine,
	Cosine,
	Sign,
	ArcTangent,
	Power,
	Minimum,
	Maximum,
	SmoothMinimum,
	PositiveSquare,
	Less,
	LessEqual,
	Equal,
	NotEqual,
	And,
	Or,
	Not,
	Select,
};

struct Step {
	Opcode opcode;
	std::uint32_t a;
	std::uint32_t b;
	std::uint32_t c;
	double value;
	std::uint32_t leaf;
};

struct Program {
	std::string name;
	std::vector<Step> steps;
	std::uint32_t result;
	std::uint32_t arguments;
};

constexpr std::uint32_t noLeaf = 0xFFFFFFFFu;

/**
 * Where a program's value at a point comes from.
 */
struct Source {
	/**
	 * The leaf whose value decides the program there, noLeaf for none.
	 */
	std::uint32_t leaf;
	/**
	 * The leaf across the smooth join the point is in the band of, or
	 * noLeaf away from one.
	 */
	std::uint32_t other;
	/**
	 * How much of the other leaf the point is made of: 0.5 (r - |a - b|)
	 * / r in the band of a smooth join of radius r, and 0 away from
	 * one.
	 */
	double share;
};

/**
 * A program run over some of its steps only, in its own register
 * numbering.
 *
 * The kept steps, runs of consecutive steps, are run in order; a
 * rerouted step is not run, its register takes the value of the step it
 * stands for, so the kept steps that read it read that value. With no
 * entries the whole program is run.
 */
struct KeptSteps {
	/**
	 * The whole program whose steps are run.
	 */
	const Program& program;
	/**
	 * A pair (first step, one past the last) for each run of kept
	 * steps in ascending order, then a pair (step, the step it stands
	 * for) for each rerouted step in ascending order of the step; null
	 * for the whole program.
	 */
	const std::uint16_t* entries;
	/**
	 * How many runs of steps are kept.
	 */
	std::uint32_t ranges;
	/**
	 * How many steps are rerouted.
	 */
	std::uint32_t rerouted;
	/**
	 * The step whose value is the answer.
	 */
	std::uint32_t result;
};

/**
 * Many points a program is evaluated at together, each with a zero
 * normal.
 */
struct PointBatch {
	/**
	 * The points, one after another.
	 */
	const double (*points)[3];
	/**
	 * How many points there are.
	 */
	std::size_t count;
	/**
	 * The arguments every point reads.
	 */
	const double* arguments;
};

int operandCount(Opcode opcode);
Step renumberStep(const Step& step, const std::vector<std::uint32_t>& map);
template <typename T>
T evaluate(const Program& program, const Inputs<T>& inputs, std::vector<T>& registers);

/**
 * Evaluate a program at many points at once, step by step over all of
 * them, and return the program's value at each, in the points' order.
 *
 * Each point's value is the one evaluate gives there, to the bit; the
 * registers hold every step's value at every point, a step's values
 * together.
 *
 * @param program The program.
 * @param batch The points and the arguments.
 * @param registers The value of each step at each point.
 */
const double* evaluatePoints(const Program& program, const PointBatch& batch, std::vector<double>& registers);

/**
 * Evaluate a program whose first steps' values the registers already
 * hold: only the steps after them are run.
 *
 * The registers must hold the values those steps have for the same
 * inputs, as when the program begins with the steps of one evaluated
 * before.
 *
 * @param program The program.
 * @param inputs The point, normal and arguments.
 * @param registers The values of the first steps, then of every step.
 */
template <typename T>
T resumedEvaluation(const Program& program, const Inputs<T>& inputs, std::vector<T>& registers);

/**
 * Return a view that runs the whole of a program.
 *
 * @param program The program.
 */
KeptSteps wholeSteps(const Program& program);

/**
 * Evaluate a program over its kept steps, in its own register numbering.
 *
 * The registers are grown to the program's length and only the kept and
 * rerouted steps' registers are written; the others keep what they held.
 *
 * @param kept The program and the steps it keeps.
 * @param inputs The point, normal and arguments.
 * @param registers The value of each step, written at the kept ones.
 */
template <typename T>
T evaluateKept(const KeptSteps& kept, const Inputs<T>& inputs, std::vector<T>& registers);

/**
 * Return the step a rerouted step stands for, or the step itself where
 * it is not rerouted.
 *
 * @param kept The program and the steps it keeps.
 * @param step The step.
 */
std::uint32_t reroutedStep(const KeptSteps& kept, std::uint32_t step);

/**
 * Find where a program's value comes from over its kept steps, given the
 * registers of every kept step at the point, as sourceOf does, passing
 * over every rerouted step to the one it stands for. Only the registers'
 * values are read (math::laneValue()), so a point's or a dual's registers
 * serve alike.
 *
 * @param kept The program and the steps it keeps.
 * @param values The registers of each step at the point, valid at the
 *        kept and rerouted ones.
 */
template <typename T>
Source keptSourceOf(const KeptSteps& kept, const std::vector<T>& values);

/**
 * Find where a program's value comes from over its kept steps at one
 * point of a batch, as keptSourceOf() does for that point alone.
 *
 * @param kept The program and the steps it keeps.
 * @param registers The dual registers of each step at the batch's
 *        points, valid at the kept and rerouted ones.
 * @param lane Which point of the batch.
 */
Source keptBatchSourceOf(const KeptSteps& kept, const std::vector<math::DualBatch>& registers, int lane);

/**
 * Return a program without the steps its result does not read.
 *
 * @param program The program.
 */
Program pruneProgram(Program program);

/**
 * Return a program without the steps its result does not read, as
 * pruneProgram does, for a program whose steps already hold zero in
 * the operands their opcodes do not read.
 *
 * A program whose every step is read comes back as it is.
 *
 * @param program The program.
 */
Program prunedCompact(Program program);

/**
 * Return a program in which every step after one reads another step's
 * value in its place.
 *
 * The step itself stays and nothing reads it, so the answer is the
 * program with that step's value replaced, a side of a join where the
 * step is the join and the target one of its operands.
 *
 * @param program The program.
 * @param step The step whose readers are redirected.
 * @param target The step they read instead, before the step.
 */
Program aliasedStep(Program program, std::uint32_t step, std::uint32_t target);

/**
 * Return whether two programs are the same step for step, every value
 * to the bit and every step's leaf alike, with the same result.
 *
 * @param a The first program.
 * @param b The second program.
 */
bool sameProgram(const Program& a, const Program& b);

/**
 * Find where a program's value at a point comes from.
 *
 * It runs the program at the point, then goes back from the result
 * through the joins: a minimum or a smooth minimum to its smaller
 * operand, a maximum to its larger, a negation to its operand and a
 * select to the side it chose. The first other step names its leaf. In
 * the band of a smooth minimum the other operand's leaf and its share
 * are kept, the outermost band first.
 *
 * @param program The program.
 * @param p The point.
 */
Source sourceAt(const Program& program, const math::Vector& p);

/**
 * Find where a program's value comes from, given the value of every
 * step at the point, as sourceAt does.
 *
 * @param program The program.
 * @param values The value of each of its steps at the point.
 */
Source sourceOf(const Program& program, const std::vector<double>& values);

}
