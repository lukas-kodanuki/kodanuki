#include "tree/face.h"
#include "sdf/numbers.h"
#include "sdf/scene.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace kodanuki {

constexpr std::size_t mostCandidates = 10;
constexpr std::size_t mostVisits     = 1000;
constexpr double sameTolerance       = 1e-12;

using Table = std::bitset<1024>;

enum class Polarity : std::uint8_t {
	Unused,
	Rising,
	Falling,
	Mixed,
};

struct Candidate {
	Program program;
	Quadric quadric;
	std::array<double, 19> signature;
	std::size_t largest;
	double scale;
	std::optional<Ring> ring;
	std::uint32_t leaf;
	bool shared;
	std::uint32_t owner;
};

struct Known {
	Program program;
	Table table;
	std::size_t candidate;
};

struct Gather {
	const Box& box;
	std::vector<Candidate> candidates;
	std::vector<Known> known;
	std::size_t visits;
	std::size_t depth;
	bool crowded;
	std::uint32_t owner;
};

struct Merger {
	std::size_t keep;
	std::size_t drop;
};

struct HeldRewrite {
	Rewritten rewritten;
	bool ready;
};

struct Split {
	const Program& program;
	const std::vector<math::Interval>& intervals;
	const Table& path;
	std::uint32_t step;
	bool settled;
	HeldRewrite& rewrite;
};

struct Side {
	const Program& program;
	KnownSteps known;
	const Split* split;
};

Table allSet() {
	return Table().set();
}

Table computedPattern(std::size_t face) {
	Table pattern;
	for (std::size_t i = 0; i < pattern.size(); ++i) {
		pattern[i] = ((i >> face) & 1) != 0;
	}
	return pattern;
}

std::array<Table, mostCandidates> computedPatterns() {
	std::array<Table, mostCandidates> patterns;
	for (std::size_t face = 0; face < mostCandidates; ++face) {
		patterns[face] = computedPattern(face);
	}
	return patterns;
}

const Table& patternOf(std::size_t face) {
	static const std::array<Table, mostCandidates> patterns = computedPatterns();
	return patterns[face];
}

Program rootedAt(const Program& program, std::uint32_t step) {
	Program rooted = program;
	rooted.result  = step;
	return prunedCompact(std::move(rooted));
}

Program withStep(const Program& program, const Step& step) {
	Program extended = program;
	extended.steps.push_back(step);
	extended.result = std::uint32_t(extended.steps.size() - 1);
	return extended;
}

Program replacedStep(Program program, std::uint32_t index, const Step& step) {
	program.steps[index] = step;
	return program;
}

Step differenceStep(const Step& crease) {
	Step difference   = crease;
	difference.opcode = Opcode::Subtract;
	difference.c      = 0;
	return difference;
}

Step negationStep(const Step& crease) {
	Step negation   = crease;
	negation.opcode = Opcode::Negate;
	negation.b      = 0;
	negation.c      = 0;
	return negation;
}

Step squareStep(const Step& crease) {
	Step square   = crease;
	square.opcode = Opcode::Multiply;
	square.b      = crease.a;
	return square;
}

Step constantStepOf(const Step& crease, double value) {
	return Step {
		.opcode = Opcode::Constant,
		.a      = 0,
		.b      = 0,
		.c      = 0,
		.value  = value,
		.leaf   = crease.leaf,
	};
}

double sampleCoordinate(const Box& box, int k, int axis) {
	if (k == 8) {
		return boxCentre(box, axis);
	}
	return ((k >> axis) & 1) != 0 ? box.axes[axis].upper : box.axes[axis].lower;
}

double programAtPoint(const Program& program, const double* p) {
	const Inputs<double> inputs = {
		.arguments = nullptr,
		.p         = { p[0], p[1], p[2] },
		.n         = { 0, 0, 0 },
	};

	thread_local std::vector<double> registers;
	return evaluate(program, inputs, registers);
}

struct Squared {
	Program program;
	double scale;
};

bool reaches(const Program& program, std::uint32_t from, std::uint32_t step) {
	std::vector<bool> reached(program.steps.size(), false);
	reached[step] = true;
	for (std::uint32_t i = step + 1; i <= from; ++i) {
		const Step& here               = program.steps[i];
		const std::uint32_t indices[3] = { here.a, here.b, here.c };
		for (int k = 0; k < operandCount(here.opcode); ++k) {
			reached[i] = reached[i] || reached[indices[k]];
		}
	}
	return from >= step && reached[from];
}

bool isConstantStep(const Program& program, std::uint32_t step) {
	return program.steps[step].opcode == Opcode::Constant;
}

std::optional<std::uint32_t> affineRoot(const Program& program, std::uint32_t step);

std::optional<std::uint32_t> rootBeside(const Program& program, std::uint32_t inside, std::uint32_t beside) {
	const std::optional<std::uint32_t> root = affineRoot(program, inside);
	if (root && !reaches(program, beside, *root)) {
		return root;
	}
	return std::nullopt;
}

std::optional<std::uint32_t> affineRoot(const Program& program, std::uint32_t step) {
	const Step& here = program.steps[step];
	switch (here.opcode) {
		case Opcode::SquareRoot: return step;
		case Opcode::Negate: return affineRoot(program, here.a);
		case Opcode::Divide: return isConstantStep(program, here.b) ? affineRoot(program, here.a) : std::nullopt;
		case Opcode::Multiply: return isConstantStep(program, here.b) ? affineRoot(program, here.a) : isConstantStep(program, here.a) ? affineRoot(program, here.b)
																																	  : std::nullopt;
		case Opcode::Add:
		case Opcode::Subtract: {
			const std::optional<std::uint32_t> left = rootBeside(program, here.a, here.b);
			return left ? left : rootBeside(program, here.b, here.a);
		}
		default: return std::nullopt;
	}
}

Program extended(const Program& program, Opcode opcode, std::uint32_t b) {
	Step step = binaryStep(opcode, program.result, b);
	step.leaf = program.steps[program.result].leaf;
	return withStep(program, step);
}

double rootSlope(const Program& program, std::uint32_t root, const double* p) {
	const Step& step = program.steps[root];
	return programAtPoint(replacedStep(program, root, constantStepOf(step, 1)), p) - programAtPoint(replacedStep(program, root, constantStepOf(step, 0)), p);
}

std::optional<Squared> squaredOf(const Program& program, const Box& box) {
	const std::optional<std::uint32_t> root = affineRoot(program, program.result);
	if (!root) {
		return std::nullopt;
	}
	const double p[3]       = { boxCentre(box, 0), boxCentre(box, 1), boxCentre(box, 2) };
	const double slope      = rootSlope(program, *root, p);
	const std::uint32_t sum = program.steps[*root].a;
	const Program rest      = replacedStep(program, *root, constantStepOf(program.steps[*root], 0));
	const Program withSlope = withStep(rest, constantStepOf(program.steps[program.result], slope));
	Program reach           = extended(withSlope, Opcode::Divide, withSlope.result);
	reach.steps.back().a    = rest.result;
	const Program squared   = extended(reach, Opcode::Multiply, reach.result);
	Program shifted         = extended(squared, Opcode::Subtract, sum);
	shifted.steps.back().a  = slope > 0 ? sum : squared.result;
	shifted.steps.back().b  = slope > 0 ? squared.result : sum;
	const double nearest    = -intervalsOver(shifted, box)[reach.result].upper;
	if (!(slope != 0 && nearest > 0)) {
		return std::nullopt;
	}
	return Squared {
		.program = prunedCompact(std::move(shifted)),
		.scale   = nearest / std::abs(slope),
	};
}

Squared surfaceOf(const Program& program, const Box& box) {
	const std::optional<Squared> squared = squaredOf(program, box);
	if (squared) {
		return *squared;
	}
	return Squared {
		.program = program,
		.scale   = 1,
	};
}

bool isAffine(const Program& program);

bool readsPoint(const Program& program) {
	for (const Step& step : prunedCompact(program).steps) {
		if (step.opcode == Opcode::PointX || step.opcode == Opcode::PointY || step.opcode == Opcode::PointZ) {
			return true;
		}
	}
	return false;
}

struct Linear {
	std::uint32_t root;
	double slope;
	double offset;
};

std::optional<Linear> rootLinear(const Program& program) {
	const std::optional<std::uint32_t> root = affineRoot(program, program.result);
	if (!root) {
		return std::nullopt;
	}
	const Program zero = replacedStep(program, *root, constantStepOf(program.steps[*root], 0));
	const Program one  = replacedStep(program, *root, constantStepOf(program.steps[*root], 1));
	if (readsPoint(zero) || readsPoint(one)) {
		return std::nullopt;
	}
	const double origin[3] = { 0, 0, 0 };
	const double offset    = programAtPoint(zero, origin);
	return Linear {
		.root   = *root,
		.slope  = programAtPoint(one, origin) - offset,
		.offset = offset,
	};
}

struct Squares {
	Program first;
	Program second;
};

std::optional<Squares> sumOfSquares(const Program& program, std::uint32_t step) {
	const Step& sum = program.steps[step];
	if (sum.opcode != Opcode::Add) {
		return std::nullopt;
	}
	const Step& a = program.steps[sum.a];
	const Step& b = program.steps[sum.b];
	if (a.opcode != Opcode::Multiply || b.opcode != Opcode::Multiply || a.a != a.b || b.a != b.b) {
		return std::nullopt;
	}
	return Squares {
		.first  = rootedAt(program, a.a),
		.second = rootedAt(program, b.a),
	};
}

struct Torus {
	std::array<Program, 3> frame;
	double major;
	double minor;
	double height;
	bool turned;
};

std::optional<Torus> torusAround(const Program& tube, const Program& height, const Linear& outer) {
	const std::optional<Linear> inner = rootLinear(tube);
	if (!inner || tube.steps[inner->root].opcode != Opcode::SquareRoot || !isAffine(height)) {
		return std::nullopt;
	}
	const std::optional<Squares> radial = sumOfSquares(tube, tube.steps[inner->root].a);
	if (!radial || !isAffine(radial->first) || !isAffine(radial->second)) {
		return std::nullopt;
	}
	const double scale = std::abs(inner->slope);
	const double major = -inner->offset / inner->slope;
	const double minor = -outer.offset / (outer.slope * scale);
	if (!(major > 0) || !(minor > 0) || !(minor < major)) {
		return std::nullopt;
	}
	return Torus {
		.frame  = { radial->first, radial->second, height },
		.major  = major,
		.minor  = minor,
		.height = scale,
		.turned = outer.slope < 0,
	};
}

std::optional<Torus> torusOf(const Program& piece) {
	const Program program             = prunedCompact(piece);
	const std::optional<Linear> outer = rootLinear(program);
	if (!outer || program.steps[outer->root].opcode != Opcode::SquareRoot) {
		return std::nullopt;
	}
	const std::optional<Squares> parts = sumOfSquares(program, program.steps[outer->root].a);
	if (!parts) {
		return std::nullopt;
	}
	const std::optional<Torus> torus = torusAround(parts->first, parts->second, *outer);
	return torus ? torus : torusAround(parts->second, parts->first, *outer);
}

std::array<double, 3> frameAt(const Torus& torus, const double* p) {
	const double scales[3] = { torus.major, torus.major, torus.major * torus.height };
	std::array<double, 3> v;
	for (int k = 0; k < 3; ++k) {
		v[std::size_t(k)] = programAtPoint(torus.frame[std::size_t(k)], p) / scales[k];
	}
	return v;
}

Ring ringIn(const Torus& torus, const Box& box) {
	const double lower[3]             = { box.axes[0].lower, box.axes[1].lower, box.axes[2].lower };
	const std::array<double, 3> start = frameAt(torus, lower);

	Ring ring = {
		.origin = { start[0], start[1], start[2] },
		.axes   = {},
		.minor  = torus.minor / torus.major,
		.turned = torus.turned,
	};

	for (int axis = 0; axis < 3; ++axis) {
		double corner[3]                = { lower[0], lower[1], lower[2] };
		corner[axis]                    = box.axes[axis].upper;
		const std::array<double, 3> end = frameAt(torus, corner);
		for (int k = 0; k < 3; ++k) {
			ring.axes[axis][k] = end[std::size_t(k)] - start[std::size_t(k)];
		}
	}
	return ring;
}

std::optional<Ring> ringOf(const Program& piece, const Box& box) {
	const std::optional<Torus> torus = torusOf(piece);
	if (!torus) {
		return std::nullopt;
	}
	return ringIn(*torus, box);
}

std::size_t largestEntry(const std::array<double, 19>& signature) {
	std::size_t largest = 0;
	for (std::size_t i = 1; i < signature.size(); ++i) {
		largest = std::abs(signature[i]) > std::abs(signature[largest]) ? i : largest;
	}
	return largest;
}

std::uint32_t pieceLeaf(const Program& program) {
	std::uint32_t at = program.result;
	while (program.steps[at].opcode == Opcode::Negate) {
		at = program.steps[at].a;
	}
	return program.steps[at].leaf;
}

Candidate candidateOf(const Program& program, const Gather& gather) {
	const Box& box         = gather.box;
	const Squared& surface = surfaceOf(program, box);

	Candidate candidate = {
		.program   = surface.program,
		.quadric   = taylorQuadric(surface.program, box),
		.signature = {},
		.largest   = 0,
		.scale     = surface.scale,
		.ring      = ringOf(program, box),
		.leaf      = pieceLeaf(program),
		.shared    = false,
		.owner     = gather.owner,
	};

	double points[9][3];
	for (int k = 0; k < 9; ++k) {
		for (int axis = 0; axis < 3; ++axis) {
			points[k][axis] = sampleCoordinate(box, k, axis);
		}
	}

	const PointBatch batch = {
		.points    = points,
		.count     = 9,
		.arguments = nullptr,
	};

	thread_local std::vector<double> registers;
	const double* samples = evaluatePoints(surface.program, batch, registers);
	std::copy(candidate.quadric.coefficients, candidate.quadric.coefficients + 10, candidate.signature.begin());
	std::copy(samples, samples + 9, candidate.signature.begin() + 10);
	candidate.largest = largestEntry(candidate.signature);
	return candidate;
}

double ratioOf(const Candidate& a, const Candidate& b) {
	const std::size_t k = a.largest;
	const double ratio  = a.signature[k] == 0 ? 0 : b.signature[k] / a.signature[k];
	const double scale  = std::abs(b.signature[b.largest]);
	for (std::size_t i = 0; i < a.signature.size(); ++i) {
		if (std::abs(b.signature[i] - ratio * a.signature[i]) > sameTolerance * scale) {
			return 0;
		}
	}
	return ratio;
}

Table unsupported(Gather& gather) {
	gather.crowded = true;
	return Table();
}

const Known* knownFace(const Gather& gather, const Program& program) {
	for (const Known& known : gather.known) {
		if (sameProgram(known.program, program)) {
			return &known;
		}
	}
	return nullptr;
}

void ownedBy(Gather& gather, std::size_t candidate) {
	if (candidate < gather.candidates.size() && gather.candidates[candidate].owner != gather.owner) {
		gather.candidates[candidate].owner = noOwner;
	}
}

struct Matched {
	Table table;
	std::size_t candidate;
};

Matched candidateTable(Gather& gather, const Program& program) {
	const Candidate candidate = candidateOf(program, gather);
	for (std::size_t j = 0; j < gather.candidates.size(); ++j) {
		const double ratio = ratioOf(gather.candidates[j], candidate);
		if (ratio != 0) {
			gather.candidates[j].shared = gather.candidates[j].shared || (ratio > 0 && gather.candidates[j].leaf != candidate.leaf);
			ownedBy(gather, j);
			return Matched {
				.table     = ratio < 0 ? ~patternOf(j) : patternOf(j),
				.candidate = j,
			};
		}
	}
	if (gather.candidates.size() == mostCandidates) {
		return Matched {
			.table     = unsupported(gather),
			.candidate = mostCandidates,
		};
	}
	gather.candidates.push_back(candidate);
	return Matched {
		.table     = patternOf(gather.candidates.size() - 1),
		.candidate = gather.candidates.size() - 1,
	};
}

Table faceTable(Gather& gather, const Program& program) {
	const Known* known = knownFace(gather, program);
	if (known) {
		ownedBy(gather, known->candidate);
		return known->table;
	}
	const Matched matched = candidateTable(gather, program);

	const Known found = {
		.program   = program,
		.table     = matched.table,
		.candidate = matched.candidate,
	};

	gather.known.push_back(found);
	return found.table;
}

Program sideOf(const Split& split, bool holds, Program base) {
	const Step& step  = split.program.steps[split.step];
	const double jump = split.intervals[split.step].upper;
	switch (step.opcode) {
		case Opcode::Minimum: return aliasedStep(std::move(base), split.step, holds ? step.a : step.b);
		case Opcode::Maximum: return aliasedStep(std::move(base), split.step, holds ? step.b : step.a);
		case Opcode::Select: return aliasedStep(std::move(base), split.step, holds ? step.b : step.c);
		case Opcode::Absolute: return holds ? replacedStep(std::move(base), split.step, negationStep(step)) : aliasedStep(std::move(base), split.step, step.a);
		case Opcode::PositiveSquare: return replacedStep(std::move(base), split.step, holds ? constantStepOf(step, 0) : squareStep(step));
		case Opcode::Sign: return replacedStep(std::move(base), split.step, constantStepOf(step, holds ? -1 : 1));
		case Opcode::Floor: return replacedStep(std::move(base), split.step, constantStepOf(step, holds ? jump - 1 : jump));
		default: return replacedStep(std::move(base), split.step, constantStepOf(step, holds ? 1 : 0));
	}
}

bool isCrease(Opcode opcode) {
	switch (opcode) {
		case Opcode::Minimum:
		case Opcode::Maximum:
		case Opcode::Absolute:
		case Opcode::Floor:
		case Opcode::Sign:
		case Opcode::Less:
		case Opcode::LessEqual:
		case Opcode::Equal:
		case Opcode::NotEqual:
		case Opcode::And:
		case Opcode::Or:
		case Opcode::Not:
		case Opcode::Select: return true;
		default: return false;
	}
}

bool isLiftable(const Program& program, std::uint32_t index) {
	const Step& step   = program.steps[index];
	const bool joining = step.opcode == Opcode::Minimum || step.opcode == Opcode::Maximum;
	return joining && program.steps[step.a].opcode != Opcode::Constant && program.steps[step.b].opcode != Opcode::Constant;
}

Polarity flipped(Polarity polarity) {
	switch (polarity) {
		case Polarity::Rising: return Polarity::Falling;
		case Polarity::Falling: return Polarity::Rising;
		default: return polarity;
	}
}

Polarity merged(Polarity a, Polarity b) {
	if (a == Polarity::Unused) {
		return b;
	}
	if (b == Polarity::Unused || a == b) {
		return a;
	}
	return Polarity::Mixed;
}

Polarity scaledBy(Polarity polarity, const math::Interval& factor) {
	if (factor.lower >= 0) {
		return polarity;
	}
	if (factor.upper <= 0) {
		return flipped(polarity);
	}
	return Polarity::Mixed;
}

Polarity dividedBy(Polarity polarity, const math::Interval& divisor) {
	if (divisor.lower > 0) {
		return polarity;
	}
	if (divisor.upper < 0) {
		return flipped(polarity);
	}
	return Polarity::Mixed;
}

std::array<Polarity, 3> operandPolarities(const Step& step, const std::vector<math::Interval>& intervals, Polarity p) {
	const Polarity mixed = Polarity::Mixed;
	switch (step.opcode) {
		case Opcode::Add:
		case Opcode::Minimum:
		case Opcode::Maximum:
		case Opcode::SmoothMinimum: return { p, p, mixed };
		case Opcode::Subtract: return { p, flipped(p), mixed };
		case Opcode::Negate: return { flipped(p), mixed, mixed };
		case Opcode::SquareRoot:
		case Opcode::PositiveSquare: return { p, mixed, mixed };
		case Opcode::Multiply: return { scaledBy(p, intervals[step.b]), scaledBy(p, intervals[step.a]), mixed };
		case Opcode::Divide: return { dividedBy(p, intervals[step.b]), mixed, mixed };
		case Opcode::Select: return { mixed, p, p };
		default: return { mixed, mixed, mixed };
	}
}

Polarity polarityAt(const Program& program, const std::vector<math::Interval>& intervals, std::uint32_t at) {
	std::vector<Polarity> polarity(program.steps.size(), Polarity::Unused);
	polarity[program.result] = Polarity::Rising;
	for (std::size_t i = program.steps.size(); i-- > std::size_t(at) + 1;) {
		if (polarity[i] == Polarity::Unused) {
			continue;
		}
		const Step& step                       = program.steps[i];
		const std::array<Polarity, 3> operands = operandPolarities(step, intervals, polarity[i]);
		const std::uint32_t indices[3]         = { step.a, step.b, step.c };
		for (int k = 0; k < operandCount(step.opcode); ++k) {
			polarity[indices[k]] = merged(polarity[indices[k]], operands[k]);
		}
	}
	return polarity[at];
}

bool isDefinite(Polarity polarity) {
	return polarity == Polarity::Rising || polarity == Polarity::Falling;
}

bool hasSingularRoot(const Program& program, const std::vector<math::Interval>& intervals) {
	for (const Step& step : program.steps) {
		if (step.opcode == Opcode::SquareRoot && intervals[step.a].lower <= 0) {
			return true;
		}
	}
	return false;
}

bool splitsAt(Opcode opcode, bool singular) {
	return isCrease(opcode) || (singular && opcode == Opcode::PositiveSquare);
}

std::optional<std::uint32_t> lastCrease(const Program& program, bool singular) {
	for (std::size_t i = program.steps.size(); i-- > 0;) {
		if (splitsAt(program.steps[i].opcode, singular)) {
			return std::uint32_t(i);
		}
	}
	return std::nullopt;
}

Table signOf(Gather& gather, const Side& side, const Table& path);

const std::vector<math::Interval>& noIntervals() {
	static const std::vector<math::Interval> none;
	return none;
}

Side wholeSide(const Program& program) {
	const KnownSteps known = {
		.intervals = noIntervals(),
		.count     = 0,
	};

	return Side {
		.program = program,
		.known   = known,
		.split   = nullptr,
	};
}

Side sharedSide(const Program& program, const Split& split, std::uint32_t count) {
	const KnownSteps known = {
		.intervals = split.intervals,
		.count     = count,
	};

	return Side {
		.program = program,
		.known   = known,
		.split   = &split,
	};
}

Program& frameSide(const Gather& gather) {
	thread_local std::deque<Program> sides;
	while (sides.size() <= gather.depth) {
		sides.emplace_back();
	}
	return sides[gather.depth];
}

Table signOfSide(Gather& gather, const Split& split, bool holds, const Table& path) {
	Program& side = frameSide(gather);
	side          = split.program;
	side          = sideOf(split, holds, std::move(side));
	return signOf(gather, sharedSide(side, split, split.step), path);
}

Table signOfWhole(Gather& gather, const Program& program, const Table& path) {
	return signOf(gather, wholeSide(program), path);
}

Table signOfExtended(Gather& gather, const Split& split, const Program& extended) {
	return signOf(gather, sharedSide(extended, split, std::uint32_t(split.program.steps.size())), split.path);
}

Table liftedTable(Gather& gather, const Split& split, Polarity polarity) {
	const bool lower  = split.program.steps[split.step].opcode == Opcode::Minimum;
	const bool either = lower == (polarity == Polarity::Rising);
	const Table holds = signOfSide(gather, split, true, split.path);
	const Table fails = signOfSide(gather, split, false, split.path);
	return either ? (holds | fails) : (holds & fails);
}

Table truthOf(Gather& gather, const Program& program, const Table& path);

Table truthOfRoot(Gather& gather, const Program& program, const Table& path) {
	const Step& root = program.steps[program.result];
	switch (root.opcode) {
		case Opcode::Less:
		case Opcode::LessEqual: return signOfWhole(gather, withStep(program, differenceStep(root)), path);
		case Opcode::And: return truthOf(gather, rootedAt(program, root.a), path) & truthOf(gather, rootedAt(program, root.b), path);
		case Opcode::Or: return truthOf(gather, rootedAt(program, root.a), path) | truthOf(gather, rootedAt(program, root.b), path);
		case Opcode::Not: return ~truthOf(gather, rootedAt(program, root.a), path);
		default: return lastCrease(program, false) ? unsupported(gather) : allSet();
	}
}

Table truthOf(Gather& gather, const Program& program, const Table& path) {
	const std::vector<math::Interval> intervals = intervalsOver(program, gather.box);
	const math::Interval& range                 = intervals[program.result];
	if (range.lower > 0 || range.upper < 0) {
		return allSet();
	}
	if (range.lower == 0 && range.upper == 0) {
		return Table();
	}
	return truthOfRoot(gather, occupiedCutBy(program, intervals).program, path);
}

bool singleJump(const Split& split) {
	const math::Interval& range = split.intervals[split.step];
	return range.upper - range.lower == 1;
}

Program floorCondition(const Split& split) {
	const Step& step       = split.program.steps[split.step];
	const Program withJump = withStep(split.program, constantStepOf(step, split.intervals[split.step].upper));
	Step difference        = differenceStep(step);
	difference.b           = withJump.result;
	return withStep(withJump, difference);
}

const Program& framedWithStep(const Gather& gather, const Program& program, const Step& step) {
	thread_local std::deque<Program> extensions;
	while (extensions.size() <= gather.depth) {
		extensions.emplace_back();
	}
	Program& extended  = extensions[gather.depth];
	extended.name      = program.name;
	extended.arguments = program.arguments;
	extended.steps.assign(program.steps.begin(), program.steps.end());
	extended.steps.push_back(step);
	extended.result = std::uint32_t(extended.steps.size() - 1);
	return extended;
}

Table conditionTable(Gather& gather, const Split& split) {
	const Step& step = split.program.steps[split.step];
	switch (step.opcode) {
		case Opcode::Minimum:
		case Opcode::Maximum:
		case Opcode::Less:
		case Opcode::LessEqual: return signOfExtended(gather, split, framedWithStep(gather, split.program, differenceStep(step)));
		case Opcode::Absolute:
		case Opcode::Sign:
		case Opcode::PositiveSquare: return signOfWhole(gather, rootedAt(split.program, step.a), split.path);
		case Opcode::Floor: return singleJump(split) ? signOfExtended(gather, split, floorCondition(split)) : unsupported(gather);
		case Opcode::Select: return truthOf(gather, rootedAt(split.program, step.a), split.path);
		case Opcode::And:
		case Opcode::Or:
		case Opcode::Not: return truthOf(gather, rootedAt(split.program, split.step), split.path);
		default: return unsupported(gather);
	}
}

Table splitTable(Gather& gather, const Split& split) {
	const Table condition = conditionTable(gather, split);
	const Table within    = condition & split.path;
	const Table outside   = ~condition & split.path;
	if (within == split.path) {
		return signOfSide(gather, split, true, split.path);
	}
	if (outside == split.path) {
		return signOfSide(gather, split, false, split.path);
	}
	const Table holds = signOfSide(gather, split, true, within);
	const Table fails = signOfSide(gather, split, false, outside);
	return (condition & holds) | (~condition & fails);
}

struct Sided {
	const Program& program;
	const std::vector<math::Interval>* intervals;
};

Table splitOrLifted(Gather& gather, const Split& split) {
	const Polarity polarity = polarityAt(split.program, split.intervals, split.step);
	const bool lifts        = isDefinite(polarity) && isLiftable(split.program, split.step);
	return lifts ? liftedTable(gather, split, polarity) : splitTable(gather, split);
}

std::vector<math::Interval>& frameIntervals(const Gather& gather, std::size_t which) {
	thread_local std::deque<std::vector<math::Interval>> frames;
	const std::size_t slot = 2 * gather.depth + which;
	while (frames.size() <= slot) {
		frames.emplace_back();
	}
	return frames[slot];
}

const std::vector<math::Interval>& cutIntervals(const Gather& gather, const Sided& sided) {
	if (sided.intervals) {
		return *sided.intervals;
	}
	std::vector<math::Interval>& own = frameIntervals(gather, 1);
	evaluate(sided.program, boxInputs(gather.box), own);
	return own;
}

HeldRewrite& frameRewrite(const Gather& gather) {
	thread_local std::deque<HeldRewrite> rewrites;
	while (rewrites.size() <= gather.depth) {
		rewrites.emplace_back();
	}
	return rewrites[gather.depth];
}

std::optional<std::uint32_t> creaseFrom(const Program& program, std::uint32_t last, const std::vector<math::Interval>& intervals) {
	if (isCrease(program.steps[last].opcode)) {
		return last;
	}
	return lastCrease(program, hasSingularRoot(program, intervals));
}

Table signOfCut(Gather& gather, const Sided& sided, const Table& path) {
	const Program& program                  = sided.program;
	const std::optional<std::uint32_t> last = lastCrease(program, true);
	if (!last) {
		return faceTable(gather, program);
	}
	const std::vector<math::Interval>& intervals = cutIntervals(gather, sided);
	const std::optional<std::uint32_t> crease    = creaseFrom(program, *last, intervals);
	if (!crease) {
		return faceTable(gather, program);
	}

	HeldRewrite& rewrite = frameRewrite(gather);
	rewrite.ready        = false;

	const Split split = {
		.program   = program,
		.intervals = intervals,
		.path      = path,
		.step      = *crease,
		.settled   = sided.intervals != nullptr,
		.rewrite   = rewrite,
	};

	return splitOrLifted(gather, split);
}

std::vector<math::Interval>& sideIntervals(const Gather& gather, const Side& side) {
	std::vector<math::Interval>& intervals = frameIntervals(gather, 0);
	intervals.assign(side.known.intervals.begin(), side.known.intervals.begin() + side.known.count);
	resumedEvaluation(side.program, boxInputs(gather.box), intervals);
	return intervals;
}

Program& frameCut(const Gather& gather) {
	thread_local std::deque<Program> cuts;
	while (cuts.size() <= gather.depth) {
		cuts.emplace_back();
	}
	return cuts[gather.depth];
}

const HeldRewrite& readyRewrite(const Split& split) {
	if (!split.rewrite.ready) {
		rewriteInto(split.program, split.intervals, split.rewrite.rewritten);
		split.rewrite.ready = true;
	}
	return split.rewrite;
}

KnownRewrite knownRewrite(const Side& side) {
	if (side.split == nullptr) {
		return KnownRewrite {
			.steps = nullptr,
			.alias = nullptr,
			.count = 0,
		};
	}
	if (side.split->settled) {
		return KnownRewrite {
			.steps = side.program.steps.data(),
			.alias = nullptr,
			.count = side.known.count,
		};
	}
	const Rewritten& rewritten = readyRewrite(*side.split).rewritten;
	return KnownRewrite {
		.steps = rewritten.steps.data(),
		.alias = rewritten.alias.data(),
		.count = side.known.count,
	};
}

Table signOfFrame(Gather& gather, const Side& side, const Table& path) {
	const std::vector<math::Interval>& intervals = sideIntervals(gather, side);
	const math::Interval& range                  = intervals[side.program.result];
	if (range.lower >= 0) {
		return Table();
	}
	if (range.upper <= 0) {
		return allSet();
	}

	const CutFrom from = {
		.intervals = intervals,
		.known     = knownRewrite(side),
	};

	Program& held = frameCut(gather);
	Cut cut       = occupiedCutInto(side.program, from, std::move(held.steps));
	held          = std::move(cut.program);

	const Sided sided = {
		.program   = held,
		.intervals = sameProgram(held, side.program) ? &intervals : nullptr,
	};

	return signOfCut(gather, sided, path);
}

Table signOf(Gather& gather, const Side& side, const Table& path) {
	gather.visits += 1;
	gather.crowded = gather.crowded || gather.visits > mostVisits;
	if (gather.crowded) {
		return Table();
	}
	gather.depth += 1;
	const Table table = signOfFrame(gather, side, path);
	gather.depth -= 1;
	return table;
}

bool dependsOn(const Table& table, std::size_t face) {
	const Table flipped = table ^ (table >> (std::size_t(1) << face));
	return (flipped & ~patternOf(face)).any();
}

std::vector<std::size_t> keptFaces(const Table& table, std::size_t count) {
	std::vector<std::size_t> kept;
	for (std::size_t j = 0; j < count; ++j) {
		if (dependsOn(table, j)) {
			kept.push_back(j);
		}
	}
	return kept;
}

std::array<std::uint32_t, tableWords> reducedTable(const Table& table, const std::vector<std::size_t>& kept) {
	std::array<std::uint32_t, tableWords> reduced = {};
	for (std::uint32_t index = 0; index < (1u << kept.size()); ++index) {
		std::size_t whole = 0;
		for (std::size_t k = 0; k < kept.size(); ++k) {
			whole |= std::size_t((index >> k) & 1) << kept[k];
		}
		reduced[index / 32] |= table[whole] ? 1u << (index % 32) : 0u;
	}
	return reduced;
}

Face faceOf(const Candidate& candidate) {
	return Face {
		.program = candidate.program,
		.quadric = candidate.quadric,
		.bound   = std::numeric_limits<double>::infinity(),
		.scale   = candidate.scale,
		.proven  = false,
		.ring    = candidate.ring,
		.shared  = candidate.shared,
		.owner   = candidate.owner,
	};
}

Faces crowdedFaces(const Gather& gather) {
	return Faces {
		.faces      = {},
		.table      = {},
		.candidates = gather.candidates.size(),
		.crowded    = true,
		.trimmed    = false,
	};
}

double sampleUnit(int k, int axis) {
	if (k == 8) {
		return 0;
	}
	return ((k >> axis) & 1) != 0 ? 1 : -1;
}

bool nearAtSamples(const Candidate& keep, const Candidate& drop, double tolerance) {
	for (int k = 0; k < 9; ++k) {
		const double u[3] = { sampleUnit(k, 0), sampleUnit(k, 1), sampleUnit(k, 2) };
		if (std::abs(drop.signature[10 + k] - quadricValue(keep.quadric, u)) > tolerance) {
			return false;
		}
	}
	return true;
}

bool approximates(const Candidate& keep, const Candidate& drop, const Cell& cell) {
	if (keep.scale != 1 || drop.scale != 1 || !nearAtSamples(keep, drop, cell.tolerance)) {
		return false;
	}

	const Proof proof = {
		.program = drop.program,
		.quadric = keep.quadric,
		.box     = cell.box,
		.limit   = cell.tolerance,
	};

	return provenBound(proof) <= cell.tolerance;
}

std::optional<Merger> mergeableFaces(const Gather& gather, const std::vector<std::size_t>& kept, const Cell& cell) {
	for (std::size_t a = 0; a < kept.size(); ++a) {
		for (std::size_t b = a + 1; b < kept.size(); ++b) {
			const Candidate& first  = gather.candidates[kept[a]];
			const Candidate& second = gather.candidates[kept[b]];
			if (approximates(first, second, cell)) {
				return Merger {
					.keep = kept[a],
					.drop = kept[b],
				};
			}
			if (approximates(second, first, cell)) {
				return Merger {
					.keep = kept[b],
					.drop = kept[a],
				};
			}
		}
	}
	return std::nullopt;
}

Table mergedTable(const Table& table, const Merger& merger) {
	Table merged;
	const std::size_t dropped = std::size_t(1) << merger.drop;
	for (std::size_t i = 0; i < merged.size(); ++i) {
		const std::size_t source = (i & ~dropped) | (((i >> merger.keep) & 1) << merger.drop);
		merged[i]                = table[source];
	}
	return merged;
}

Table mergedInside(Gather& gather, const Table& inside, const Cell& cell) {
	Table merged = inside;
	for (std::vector<std::size_t> kept = keptFaces(merged, gather.candidates.size()); kept.size() > mostFaces; kept = keptFaces(merged, gather.candidates.size())) {
		const std::optional<Merger> merger = mergeableFaces(gather, kept, cell);
		if (!merger) {
			return merged;
		}
		gather.candidates[merger->keep].owner  = gather.candidates[merger->keep].owner == gather.candidates[merger->drop].owner ? gather.candidates[merger->keep].owner : noOwner;
		merged                                 = mergedTable(merged, *merger);
		gather.candidates[merger->keep].shared = true;
	}
	return merged;
}

double centreDistance(const Candidate& candidate) {
	const double* q       = candidate.quadric.coefficients;
	const double gradient = std::sqrt(q[6] * q[6] + q[7] * q[7] + q[8] * q[8]);
	return std::abs(q[9]) / std::max(gradient, 1e-300);
}

Table fixedTable(const Table& table, std::size_t face, bool inside) {
	Table fixed;
	const std::size_t bit = std::size_t(1) << face;
	for (std::size_t i = 0; i < fixed.size(); ++i) {
		fixed[i] = table[inside ? (i | bit) : (i & ~bit)];
	}
	return fixed;
}

std::size_t farthestFace(const Gather& gather, const std::vector<std::size_t>& kept) {
	std::size_t farthest = kept[0];
	for (const std::size_t j : kept) {
		farthest = centreDistance(gather.candidates[j]) > centreDistance(gather.candidates[farthest]) ? j : farthest;
	}
	return farthest;
}

Table trimmedInside(const Gather& gather, const Table& inside) {
	Table trimmed = inside;
	for (std::vector<std::size_t> kept = keptFaces(trimmed, gather.candidates.size()); kept.size() > mostFaces; kept = keptFaces(trimmed, gather.candidates.size())) {
		const std::size_t face = farthestFace(gather, kept);
		trimmed                = fixedTable(trimmed, face, gather.candidates[face].quadric.coefficients[9] < 0);
	}
	return trimmed;
}

struct Affinity {
	bool constant;
	bool affine;
};

Affinity affinity(bool constant, bool affine) {
	return Affinity {
		.constant = constant,
		.affine   = affine,
	};
}

Affinity productAffinity(const Affinity& a, const Affinity& b) {
	return affinity(a.constant && b.constant, (a.constant && b.affine) || (b.constant && a.affine));
}

Affinity affinityOf(const Step& step, const std::vector<Affinity>& known) {
	switch (step.opcode) {
		case Opcode::Constant: return affinity(true, true);
		case Opcode::PointX:
		case Opcode::PointY:
		case Opcode::PointZ: return affinity(false, true);
		case Opcode::Negate: return known[step.a];
		case Opcode::Add:
		case Opcode::Subtract: return affinity(known[step.a].constant && known[step.b].constant, known[step.a].affine && known[step.b].affine);
		case Opcode::Multiply: return productAffinity(known[step.a], known[step.b]);
		case Opcode::Divide: return affinity(known[step.a].constant && known[step.b].constant, known[step.a].affine && known[step.b].constant);
		default: return affinity(false, false);
	}
}

bool isAffine(const Program& program) {
	std::vector<Affinity> known;
	known.reserve(program.steps.size());
	for (const Step& step : program.steps) {
		known.push_back(affinityOf(step, known));
	}
	return known[program.result].affine;
}

struct Plane {
	std::size_t face;
	std::array<double, 3> direction;
	double level;
	bool below;
};

std::optional<Plane> planeOf(const Candidate& candidate, std::size_t face) {
	const double* q     = candidate.quadric.coefficients;
	const double length = std::sqrt(q[6] * q[6] + q[7] * q[7] + q[8] * q[8]);
	if (!(length > 0) || !isAffine(candidate.program)) {
		return std::nullopt;
	}
	const std::size_t lead = std::abs(q[6]) >= std::abs(q[7]) && std::abs(q[6]) >= std::abs(q[8]) ? 6 : std::abs(q[7]) >= std::abs(q[8]) ? 7
																																		 : 8;
	const double sign      = q[lead] > 0 ? 1 : -1;
	return Plane {
		.face      = face,
		.direction = { sign * q[6] / length, sign * q[7] / length, sign * q[8] / length },
		.level     = -sign * q[9] / length,
		.below     = sign > 0,
	};
}

bool parallel(const Plane& a, const Plane& b) {
	return std::abs(a.direction[0] - b.direction[0]) + std::abs(a.direction[1] - b.direction[1]) + std::abs(a.direction[2] - b.direction[2]) < 1e-9;
}

std::vector<std::vector<Plane>> parallelGroups(const Gather& gather) {
	std::vector<std::vector<Plane>> groups;
	for (std::size_t j = 0; j < gather.candidates.size(); ++j) {
		const std::optional<Plane> plane = planeOf(gather.candidates[j], j);
		if (!plane) {
			continue;
		}
		const auto group = std::find_if(groups.begin(), groups.end(), [&](const std::vector<Plane>& planes) {
			return parallel(planes[0], *plane);
		});

		if (group == groups.end()) {
			groups.push_back({ *plane });
			continue;
		}
		group->push_back(*plane);
	}
	return groups;
}

std::size_t bitsAlong(const std::vector<Plane>& group, double t) {
	std::size_t bits = 0;
	for (const Plane& plane : group) {
		bits |= (plane.below ? t < plane.level : t > plane.level) ? std::size_t(1) << plane.face : 0;
	}
	return bits;
}

std::vector<std::size_t> realizedBits(const std::vector<Plane>& group) {
	const std::array<double, 3>& e = group[0].direction;
	const double span              = std::abs(e[0]) + std::abs(e[1]) + std::abs(e[2]);
	std::vector<double> levels     = { -span, span };
	for (const Plane& plane : group) {
		levels.push_back(std::clamp(plane.level, -span, span));
	}
	std::sort(levels.begin(), levels.end());
	std::vector<std::size_t> realized;
	for (std::size_t k = 0; k + 1 < levels.size(); ++k) {
		realized.push_back(bitsAlong(group, (levels[k] + levels[k + 1]) / 2));
	}
	return realized;
}

Table realizedPatterns(const Gather& gather) {
	const std::size_t count = std::size_t(1) << gather.candidates.size();
	Table realized          = count >= Table().size() ? allSet() : ~(allSet() << count);
	for (const std::vector<Plane>& group : parallelGroups(gather)) {
		std::size_t mask = 0;
		for (const Plane& plane : group) {
			mask |= std::size_t(1) << plane.face;
		}
		const std::vector<std::size_t> bits = realizedBits(group);
		for (std::size_t p = 0; p < count; ++p) {
			realized[p] = realized[p] && std::find(bits.begin(), bits.end(), p & mask) != bits.end();
		}
	}
	return realized;
}

struct Realized {
	Table inside;
	Table possible;
};

Realized withoutFace(const Realized& realized, std::size_t face) {
	const std::size_t shift = std::size_t(1) << face;
	const Table& r          = realized.possible;
	const Table low         = ((r & realized.inside) | (~r & (realized.inside >> shift))) & ~patternOf(face);
	const Table lowPossible = (r | (r >> shift)) & ~patternOf(face);
	return Realized {
		.inside   = low | (low << shift),
		.possible = lowPossible | (lowPossible << shift),
	};
}

bool mattersFor(const Realized& realized, std::size_t face) {
	const std::size_t shift = std::size_t(1) << face;
	const Table both        = realized.possible & (realized.possible >> shift) & ~patternOf(face);
	return ((realized.inside ^ (realized.inside >> shift)) & both).any();
}

Table realizedInside(const Gather& gather, const Table& inside) {
	Realized realized = {
		.inside   = inside,
		.possible = realizedPatterns(gather),
	};

	for (std::size_t j = 0; j < gather.candidates.size(); ++j) {
		realized = mattersFor(realized, j) ? realized : withoutFace(realized, j);
	}
	return realized.inside;
}

Faces facesOf(const Cell& cell, bool merging) {
	Gather gather = {
		.box        = cell.box,
		.candidates = {},
		.known      = {},
		.visits     = 0,
		.depth      = 0,
		.crowded    = false,
		.owner      = 0,
	};

	Table inside;
	for (const std::shared_ptr<const Program>& program : cell.programs) {
		inside |= signOfWhole(gather, *program, allSet());
		gather.owner += 1;
	}
	if (gather.crowded) {
		return crowdedFaces(gather);
	}
	inside                              = realizedInside(gather, inside);
	const Table merged                  = merging ? mergedInside(gather, inside, cell) : inside;
	const bool trimming                 = merging && keptFaces(merged, gather.candidates.size()).size() > mostFaces;
	inside                              = trimming ? trimmedInside(gather, merged) : merged;
	const std::vector<std::size_t> kept = keptFaces(inside, gather.candidates.size());
	std::vector<Face> faces;
	for (const std::size_t j : kept) {
		faces.push_back(faceOf(gather.candidates[j]));
	}
	return Faces {
		.faces      = faces,
		.table      = kept.size() <= mostFaces ? reducedTable(inside, kept) : std::array<std::uint32_t, tableWords> {},
		.candidates = gather.candidates.size(),
		.crowded    = false,
		.trimmed    = trimming,
	};
}

Faces gatheredFaces(const Cell& cell) {
	return facesOf(cell, false);
}

Faces mergedFaces(const Cell& cell) {
	return facesOf(cell, true);
}

Faces provedFaces(const Faces& faces, const Cell& cell) {
	Faces proved = faces;
	if (faces.crowded || faces.faces.size() > mostFaces) {
		return proved;
	}
	for (Face& face : proved.faces) {
		const Proof proof = {
			.program = face.program,
			.quadric = face.quadric,
			.box     = cell.box,
			.limit   = cell.tolerance * face.scale,
		};

		face.bound  = provenBound(proof);
		face.proven = face.bound <= proof.limit;
	}
	return proved;
}

Faces cellFaces(const Cell& cell) {
	return provedFaces(gatheredFaces(cell), cell);
}

bool tableInside(const Faces& faces, std::uint32_t bits) {
	return ((faces.table[bits / 32] >> (bits % 32)) & 1) != 0;
}

}
