#include "plugins/geometry/tree/classify.h"
#include "plugins/geometry/tree/pass.h"

#include "sdf/numbers.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace kodanuki {

constexpr double foldWindow = 1e-3;
constexpr double foldMargin = 1e-4;
constexpr int foldPieces    = 4;

struct FaceQuery {
	Box box;
	double tolerance;
	bool turned;
};

struct WrittenFace {
	FaceKind kind;
	std::vector<std::uint32_t> words;
	bool held;
};

Fitted blankFitted() {
	const Patch patch = {
		.weights = {},
		.step    = 0,
		.bent    = false,
	};

	return Fitted {
		.patch = patch,
		.stray = 0,
	};
}

Plan blankPlan(CellKind kind, const CellPrograms& programs) {
	return Plan {
		.kind     = kind,
		.programs = programs,
		.run      = {},
		.fitted   = blankFitted(),
		.patched  = false,
		.seamed   = false,
		.leafKind = LeafKind::Held,
		.loosened = 0,
	};
}

bool isPlaneQuadric(const Quadric& quadric) {
	for (int k = 0; k < 6; ++k) {
		if (quadric.coefficients[k] != 0) {
			return false;
		}
	}
	return true;
}

std::vector<std::uint32_t> floatWords(const Quadric& quadric, int first, double sign) {
	const Quadric corner = cornerQuadric(quadric);
	std::vector<std::uint32_t> words;
	for (int k = first; k < 10; ++k) {
		words.push_back(std::bit_cast<std::uint32_t>(float(sign * corner.coefficients[k])));
	}
	return words;
}

Patch turnedPatch(const Patch& patch, bool turned) {
	Patch result = patch;
	for (float& weight : result.weights) {
		weight = turned ? -weight : weight;
	}
	return result;
}

WrittenFace patchFace(const Face& face, const FaceQuery& query) {
	const std::vector<std::shared_ptr<const Program>> programs = { borrowedProgram(face.program) };

	const PatchQuery fit = {
		.programs = programs,
		.box      = query.box,
	};

	const Fitted fitted = fitPatch(fit);
	return WrittenFace {
		.kind  = FaceKind::Patch,
		.words = patchWords(turnedPatch(fitted.patch, query.turned)),
		.held  = fitted.stray <= query.tolerance * face.scale,
	};
}

std::vector<std::uint32_t> ringWords(const Ring& ring, bool turned) {
	std::vector<std::uint32_t> words;
	for (const double value : ring.origin) {
		words.push_back(std::bit_cast<std::uint32_t>(float(value)));
	}
	for (const auto& axis : ring.axes) {
		for (const double value : axis) {
			words.push_back(std::bit_cast<std::uint32_t>(float(value)));
		}
	}
	words.push_back(std::bit_cast<std::uint32_t>(1.0f));
	words.push_back(std::bit_cast<std::uint32_t>(float(ring.turned != turned ? -ring.minor : ring.minor)));
	return words;
}

WrittenFace ringFace(const Ring& ring, const FaceQuery& query) {
	return WrittenFace {
		.kind  = FaceKind::Ring,
		.words = ringWords(ring, query.turned),
		.held  = true,
	};
}

WrittenFace writtenFace(const Face& face, const FaceQuery& query) {
	if (!face.proven && face.ring) {
		return ringFace(*face.ring, query);
	}
	if (!face.proven) {
		return patchFace(face, query);
	}
	const double sign = query.turned ? -1 : 1;
	const bool plane  = isPlaneQuadric(face.quadric);
	return WrittenFace {
		.kind  = plane ? FaceKind::Plane : FaceKind::Quadric,
		.words = floatWords(face.quadric, plane ? 6 : 0, sign),
		.held  = true,
	};
}

bool tableBit(const std::array<std::uint32_t, tableWords>& table, std::uint32_t bits) {
	return ((table[bits / 32] >> (bits % 32)) & 1) != 0;
}

std::uint32_t neededMask(const std::array<std::uint32_t, tableWords>& table, std::uint32_t count) {
	std::uint32_t needed = (1u << count) - 1;
	for (std::uint32_t bits = 0; bits < (1u << count); ++bits) {
		needed &= tableBit(table, bits) ? bits : needed;
	}
	return needed;
}

bool singleTurned(const Faces& faces) {
	return faces.faces.size() == 1 && (faces.table[0] & 3u) == 1u;
}

std::array<std::uint32_t, tableWords> runTable(const Faces& faces) {
	std::array<std::uint32_t, tableWords> table = faces.table;
	table[0]                                    = singleTurned(faces) ? 2u : table[0];
	return table;
}

FaceRun facesRun(const RunQuery& query) {
	const std::uint32_t count                         = std::uint32_t(query.faces.faces.size());
	const bool turned                                 = singleTurned(query.faces);
	const std::array<std::uint32_t, tableWords> table = runTable(query.faces);

	RunHeader header = {
		.count     = count,
		.patches   = 0,
		.planes    = 0,
		.needed    = neededMask(table, count),
		.slabbed   = false,
		.passWords = 0,
	};

	FaceRun run = {
		.held  = true,
		.words = { 0 },
	};

	const FaceQuery asked = {
		.box       = query.box,
		.tolerance = query.tolerance,
		.turned    = turned,
	};

	run.words.insert(run.words.end(), table.begin(), table.begin() + tableWordCount(count));
	const std::vector<std::uint32_t> written = passRunWords(query);
	header.passWords                         = std::uint32_t(written.size());
	run.words.insert(run.words.end(), written.begin(), written.end());
	for (std::uint32_t j = 0; j < count; ++j) {
		const WrittenFace face = writtenFace(query.faces.faces[j], asked);
		header.patches |= face.kind == FaceKind::Patch || face.kind == FaceKind::Ring ? 1u << j : 0u;
		header.planes |= face.kind == FaceKind::Plane || face.kind == FaceKind::Ring ? 1u << j : 0u;
		run.held = run.held && face.held;
		run.words.insert(run.words.end(), face.words.begin(), face.words.end());
	}
	run.words[0] = runHeaderWord(header);
	return run;
}

FaceRun patchRun(const Fitted& fitted, double tolerance) {
	const RunHeader header = {
		.count     = 1,
		.patches   = 1,
		.planes    = 0,
		.needed    = 1,
		.slabbed   = false,
		.passWords = 0,
	};

	FaceRun run = {
		.held  = fitted.stray <= tolerance,
		.words = { runHeaderWord(header) },
	};

	const std::vector<std::uint32_t> words = patchWords(fitted.patch);
	run.words.insert(run.words.end(), words.begin(), words.end());
	return run;
}

Faces faceSearch(const CellQuery& query, const Cell& cell) {
	if (query.deeper) {
		return cellFaces(cell);
	}
	return provedFaces(mergedFaces(cell), cell);
}

std::optional<Plan> facedPlan(const CellQuery& query, const CellPrograms& programs) {
	const Cell cell = {
		.programs  = programs.programs,
		.box       = query.box,
		.tolerance = query.tolerance,
	};

	const Faces faces = faceSearch(query, cell);
	if (faces.crowded || faces.faces.size() > mostFaces) {
		return std::nullopt;
	}
	if (faces.faces.empty()) {
		return blankPlan((faces.table[0] & 1u) != 0 ? CellKind::Filled : CellKind::Empty, programs);
	}

	const RunQuery written = {
		.faces     = faces,
		.programs  = programs.programs,
		.box       = query.box,
		.tolerance = query.tolerance,
	};

	const FaceRun run = facesRun(written);
	const bool held   = run.held && !faces.trimmed;
	if (!held && query.deeper) {
		return std::nullopt;
	}
	Plan plan     = blankPlan(CellKind::Surface, programs);
	plan.run      = run.words;
	plan.leafKind = held ? LeafKind::Held : LeafKind::Floor;
	return plan;
}

struct Seam {
	std::vector<std::shared_ptr<const Program>> left;
	std::vector<std::shared_ptr<const Program>> right;
	bool larger;
};

struct SeamStep {
	std::uint32_t step;
	bool larger;
};

bool isConstant(const Program& program, std::uint32_t step) {
	return program.steps[step].opcode == Opcode::Constant;
}

bool negative(const Program& program, std::uint32_t step) {
	return program.steps[step].value < 0;
}

struct Unwrapped {
	std::uint32_t step;
	bool flipped;
	bool through;
};

Unwrapped unwrappedTo(std::uint32_t step, bool flipped, bool through) {
	return Unwrapped {
		.step    = step,
		.flipped = flipped,
		.through = through,
	};
}

Unwrapped throughConstant(const Program& program, const Step& step, bool flips) {
	if (isConstant(program, step.b)) {
		return Unwrapped {
			.step    = step.a,
			.flipped = flips && negative(program, step.b),
			.through = true,
		};
	}
	return Unwrapped {
		.step    = step.b,
		.flipped = flips && negative(program, step.a),
		.through = isConstant(program, step.a) && step.opcode != Opcode::Divide,
	};
}

Unwrapped unwrapped(const Program& program, std::uint32_t at) {
	const Step& step = program.steps[at];
	switch (step.opcode) {
		case Opcode::Negate: return unwrappedTo(step.a, true, true);
		case Opcode::Multiply:
		case Opcode::Divide: return throughConstant(program, step, true);
		case Opcode::Add: return throughConstant(program, step, false);
		case Opcode::Subtract: {
			Unwrapped inner = throughConstant(program, step, false);
			inner.flipped   = inner.step == step.b;
			return inner;
		}
		default: return unwrappedTo(at, false, false);
	}
}

bool isHardJoin(Opcode opcode) {
	return opcode == Opcode::Minimum || opcode == Opcode::Maximum;
}

std::optional<SeamStep> programJoin(const Program& program) {
	Unwrapped at = unwrappedTo(program.result, false, true);
	while (at.through && !isHardJoin(program.steps[at.step].opcode)) {
		const Unwrapped inner = unwrapped(program, at.step);
		at                    = unwrappedTo(inner.step, at.flipped != inner.flipped, inner.through);
	}
	if (!at.through) {
		return std::nullopt;
	}
	return SeamStep {
		.step   = at.step,
		.larger = (program.steps[at.step].opcode == Opcode::Maximum) != at.flipped,
	};
}

std::optional<Seam> programSeam(const Program& program) {
	const std::optional<SeamStep> join = programJoin(program);
	if (!join) {
		return std::nullopt;
	}
	const Step& step = program.steps[join->step];
	return Seam {
		.left   = { std::make_shared<const Program>(pruneProgram(aliasedStep(program, join->step, step.a))) },
		.right  = { std::make_shared<const Program>(pruneProgram(aliasedStep(program, join->step, step.b))) },
		.larger = join->larger,
	};
}

std::optional<Seam> cellSeam(const std::vector<std::shared_ptr<const Program>>& programs) {
	if (programs.size() == 1) {
		return programSeam(*programs[0]);
	}
	return Seam {
		.left   = std::vector<std::shared_ptr<const Program>>(programs.begin(), programs.end() - 1),
		.right  = { programs.back() },
		.larger = false,
	};
}

FaceRun seamRun(const SeamFit& fit, bool larger, double tolerance) {
	const RunHeader header = {
		.count     = 2,
		.patches   = 3,
		.planes    = 0,
		.needed    = larger ? 3u : 0u,
		.slabbed   = false,
		.passWords = 0,
	};

	FaceRun run = {
		.held  = fit.stray <= tolerance,
		.words = { runHeaderWord(header), larger ? 8u : 14u },
	};

	const std::vector<std::uint32_t> left  = patchWords(fit.left);
	const std::vector<std::uint32_t> right = patchWords(fit.right);
	run.words.insert(run.words.end(), left.begin(), left.end());
	run.words.insert(run.words.end(), right.begin(), right.end());
	return run;
}

struct Patched {
	FaceRun run;
	Fitted fitted;
	bool single;
};

Patched seamPatched(const CellQuery& query, const std::vector<std::shared_ptr<const Program>>& programs, const std::array<double, 35>& values) {
	const Fitted fitted = fittedTo(values);

	const Patched single = {
		.run    = patchRun(fitted, query.tolerance),
		.fitted = fitted,
		.single = true,
	};

	const std::optional<Seam> seam = single.run.held ? std::nullopt : cellSeam(programs);
	if (!seam) {
		return single;
	}

	const SeamQuery asked = {
		.whole  = values,
		.left   = seam->left,
		.right  = seam->right,
		.box    = query.box,
		.larger = seam->larger,
	};

	const SeamFit fit = fitSeam(asked);
	if (!(fit.stray < fitted.stray)) {
		return single;
	}
	return Patched {
		.run    = seamRun(fit, seam->larger, query.tolerance),
		.fitted = fitted,
		.single = false,
	};
}

Plan patchedPlan(const CellQuery& query, const CellPrograms& programs) {
	const PatchQuery fit = {
		.programs = programs.programs,
		.box      = query.box,
	};

	const Patched patched = seamPatched(query, programs.programs, fieldReadings(fit));
	if (!patched.run.held && query.deeper) {
		return blankPlan(CellKind::Branch, programs);
	}
	Plan plan     = blankPlan(CellKind::Surface, programs);
	plan.run      = patched.run.words;
	plan.fitted   = patched.fitted;
	plan.patched  = patched.single;
	plan.seamed   = !patched.single;
	plan.leafKind = patched.run.held ? LeafKind::Held : LeafKind::Floor;
	return plan;
}

CellKind cutKind(Occupancy occupancy) {
	switch (occupancy) {
		case Occupancy::Empty: return CellKind::Empty;
		case Occupancy::Filled: return CellKind::Filled;
		case Occupancy::Surface: return CellKind::Surface;
	}

	return CellKind::Empty;
}

Plan classifyCell(const CellQuery& query) {
	const CellCut cut = cutCell(query.programs, query.box);
	if (cut.occupancy != Occupancy::Surface) {
		return blankPlan(cutKind(cut.occupancy), cut.programs);
	}
	if (query.faces) {
		const std::optional<Plan> faced = facedPlan(query, cut.programs);
		if (faced) {
			return *faced;
		}
	}
	return patchedPlan(query, cut.programs);
}

math::Interval windowPiece(int piece) {
	const double step = (1 + 2 * foldWindow) / foldPieces;
	return math::Interval {
		.lower = -foldWindow + step * piece,
		.upper = -foldWindow + step * (piece + 1),
	};
}

math::Interval squaredPiece(const math::Interval& u) {
	const double low  = u.lower * u.lower;
	const double high = u.upper * u.upper;
	const bool across = u.lower < 0 && u.upper > 0;
	return math::Interval {
		.lower = across ? 0 : std::min(low, high),
		.upper = std::max(low, high),
	};
}

math::Interval scaledPiece(float factor, const math::Interval& piece) {
	const math::Interval constant = {
		.lower = double(factor),
		.upper = double(factor),
	};

	return constant * piece;
}

math::Interval quadricRange(const float* f, const math::Interval* u) {
	math::Interval range = scaledPiece(f[9], math::Interval { .lower = 1, .upper = 1 });
	for (int axis = 0; axis < 3; ++axis) {
		range = range + scaledPiece(f[axis], squaredPiece(u[axis])) + scaledPiece(f[6 + axis], u[axis]);
	}
	return range + scaledPiece(f[3], u[0] * u[1]) + scaledPiece(f[4], u[0] * u[2]) + scaledPiece(f[5], u[1] * u[2]);
}

int rangeSide(const math::Interval& range, double margin) {
	if (range.lower > margin) {
		return 1;
	}
	return range.upper < -margin ? -1 : 0;
}

int quadricSide(const float* f) {
	double margin = 0;
	for (int k = 0; k < 10; ++k) {
		margin += foldMargin * std::abs(double(f[k]));
	}
	int side = 2;
	for (int piece = 0; piece < foldPieces * foldPieces * foldPieces; ++piece) {
		const math::Interval u[3] = { windowPiece(piece % foldPieces), windowPiece(piece / foldPieces % foldPieces), windowPiece(piece / (foldPieces * foldPieces)) };
		const int here            = rangeSide(quadricRange(f, u), margin);
		if (here == 0 || (side != 2 && here != side)) {
			return 0;
		}
		side = here;
	}
	return side;
}

struct FoldedFaces {
	std::uint32_t kept;
	std::uint32_t inside;
};

FoldedFaces foldedFaces(const std::vector<std::uint32_t>& run) {
	const RunHeader header                   = runHeader(run[0]);
	const std::vector<std::uint32_t> offsets = faceOffsets(run.data());

	FoldedFaces folded = {
		.kept   = 0,
		.inside = 0,
	};

	for (std::uint32_t face = 0; face < header.count; ++face) {
		const int side = faceKind(header, face) == FaceKind::Quadric ? quadricSide(reinterpret_cast<const float*>(run.data() + offsets[face])) : 0;
		folded.kept |= side == 0 ? 1u << face : 0u;
		folded.inside |= side < 0 ? 1u << face : 0u;
	}
	return folded;
}

std::uint32_t spreadBits(std::uint32_t bits, std::uint32_t kept) {
	std::uint32_t spread = 0;
	int next             = 0;
	for (std::uint32_t rest = kept; rest != 0; rest &= rest - 1) {
		spread |= ((bits >> next) & 1u) << std::countr_zero(rest);
		next += 1;
	}
	return spread;
}

std::array<std::uint32_t, tableWords> foldedTable(const std::vector<std::uint32_t>& run, const FoldedFaces& folded) {
	std::array<std::uint32_t, tableWords> table = {};
	const std::uint32_t count                   = std::uint32_t(std::popcount(folded.kept));
	for (std::uint32_t bits = 0; bits < (1u << count); ++bits) {
		const std::uint32_t whole = spreadBits(bits, folded.kept) | folded.inside;
		table[bits / 32] |= ((run[1 + whole / 32] >> (whole % 32)) & 1u) << (bits % 32);
	}
	return table;
}

std::uint32_t keptMask(std::uint32_t mask, std::uint32_t kept) {
	std::uint32_t packed = 0;
	int next             = 0;
	for (std::uint32_t rest = kept; rest != 0; rest &= rest - 1) {
		packed |= ((mask >> std::countr_zero(rest)) & 1u) << next;
		next += 1;
	}
	return packed;
}

std::vector<std::uint32_t> foldedRun(const std::vector<std::uint32_t>& run) {
	if (run.empty() || runHeader(run[0]).count < 3) {
		return run;
	}
	const RunHeader header    = runHeader(run[0]);
	const FoldedFaces folded  = foldedFaces(run);
	const std::uint32_t count = std::uint32_t(std::popcount(folded.kept));
	if (count == header.count || count < 2) {
		return run;
	}
	const std::array<std::uint32_t, tableWords> table = foldedTable(run, folded);

	const std::vector<std::uint32_t> passed = passFoldedWords(run.data() + 1 + tableWordCount(header.count), header.passWords, folded.kept);

	const RunHeader kept = {
		.count     = count,
		.patches   = keptMask(header.patches, folded.kept),
		.planes    = keptMask(header.planes, folded.kept),
		.needed    = neededMask(table, count),
		.slabbed   = header.slabbed,
		.passWords = std::uint32_t(passed.size()),
	};

	std::vector<std::uint32_t> words = { runHeaderWord(kept) };
	words.insert(words.end(), table.begin(), table.begin() + tableWordCount(count));
	words.insert(words.end(), passed.begin(), passed.end());
	const std::vector<std::uint32_t> offsets = faceOffsets(run.data());
	for (std::uint32_t rest = folded.kept; rest != 0; rest &= rest - 1) {
		const int face = std::countr_zero(rest);
		words.insert(words.end(), run.begin() + offsets[std::size_t(face)], run.begin() + offsets[std::size_t(face) + 1]);
	}
	return words;
}

double fittedRunValue(const std::uint32_t* run, const double* u) {
	const RunHeader header                   = runHeader(run[0]);
	const std::vector<std::uint32_t> offsets = faceOffsets(run);
	const double first                       = patchValue(readPatch(run + offsets[0]), u);
	if (header.count == 1) {
		return first;
	}
	const double second = patchValue(readPatch(run + offsets[1]), u);
	return header.needed == 3 ? std::max(first, second) : std::min(first, second);
}

}
