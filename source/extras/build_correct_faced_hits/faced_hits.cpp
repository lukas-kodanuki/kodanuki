#include "extras/build_correct_faced_hits/faced_hits.h"

#include "sdf/numbers.h"
#include "sdf/program.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace kodanuki {

constexpr std::uint32_t unknownOwner = 0xFu;

constexpr int mostDescriptions = 3;

constexpr double exactShare = 1e-3;

bool blends(const Program& program) {
	for (const Step& step : program.steps) {
		if (step.opcode == Opcode::SmoothMinimum) {
			return true;
		}
	}
	return false;
}

constexpr std::uint32_t mixedLeaf = 0xFFFFFFFEu;
constexpr double sourceBand       = 1e-9;

struct SurfaceSample {
	bool found;
	double p[3];
	double gradient[3];
};

SurfaceSample projectedOnto(const Program& program, const Box& box, const double* start) {
	thread_local std::vector<math::Dual> registers;
	SurfaceSample sample  = { .found = false, .p = { start[0], start[1], start[2] }, .gradient = { 0, 0, 0 } };
	const math::Dual zero = math::constantAs<math::Dual>(0);
	double size           = 0;
	for (int axis = 0; axis < 3; ++axis) {
		size = std::max(size, box.axes[axis].upper - box.axes[axis].lower);
	}
	for (int step = 0; step < 8; ++step) {
		const Inputs<math::Dual> inputs = {
			.arguments = nullptr,
			.p         = { math::coordinateAs<math::Dual>(sample.p[0], 0), math::coordinateAs<math::Dual>(sample.p[1], 1), math::coordinateAs<math::Dual>(sample.p[2], 2) },
			.n         = { zero, zero, zero },
		};
		const math::Dual f = evaluate(program, inputs, registers);
		const double g2    = f.gradient[0] * f.gradient[0] + f.gradient[1] * f.gradient[1] + f.gradient[2] * f.gradient[2];
		if (!(g2 > 0)) {
			return sample;
		}
		for (int axis = 0; axis < 3; ++axis) {
			sample.p[axis] -= f.value * f.gradient[axis] / g2;
			sample.gradient[axis] = f.gradient[axis] / std::sqrt(g2);
		}
		if (std::abs(f.value) / std::sqrt(g2) < 1e-9 * size) {
			break;
		}
	}
	sample.found = true;
	for (int axis = 0; axis < 3; ++axis) {
		sample.found = sample.found && sample.p[axis] >= box.axes[axis].lower && sample.p[axis] <= box.axes[axis].upper;
	}
	return sample;
}

std::uint32_t tracedLeaf(const Program& program, const std::vector<double>& values, double band) {
	std::uint32_t at = program.result;
	for (;;) {
		const Step& step = program.steps[at];
		switch (step.opcode) {
			case Opcode::Negate: at = step.a; continue;
			case Opcode::Select: at = values[step.a] != 0 ? step.b : step.c; continue;
			case Opcode::Minimum:
			case Opcode::Maximum: {
				const double a = values[step.a];
				const double b = values[step.b];
				if (std::abs(a - b) <= band) {
					return mixedLeaf;
				}
				at = (step.opcode == Opcode::Minimum) == (a <= b) ? step.a : step.b;
				continue;
			}
			case Opcode::SmoothMinimum: {
				const double a = values[step.a];
				const double b = values[step.b];
				if (std::abs(a - b) <= std::max(band, values[step.c])) {
					return mixedLeaf;
				}
				at = a <= b ? step.a : step.b;
				continue;
			}
			default: return step.leaf;
		}
	}
}

struct SourceReading {
	std::uint32_t leaf;
	double value;
};

SourceReading sourceAt(const RunQuery& query, const double* p) {
	thread_local std::vector<double> registers;
	const double band       = sourceBand * (1 + std::max({ std::abs(p[0]), std::abs(p[1]), std::abs(p[2]) }));
	const Inputs<double> in = {
		.arguments = nullptr,
		.p         = { p[0], p[1], p[2] },
		.n         = { 0, 0, 0 },
	};
	double least       = std::numeric_limits<double>::infinity();
	std::uint32_t leaf = noLeaf;
	for (const std::shared_ptr<const Program>& program : query.programs) {
		const double value = evaluate(*program, in, registers);
		if (std::abs(value - least) <= band) {
			return SourceReading { .leaf = mixedLeaf, .value = value };
		}
		if (value < least) {
			least = value;
			leaf  = tracedLeaf(*program, registers, band);
		}
	}
	return SourceReading { .leaf = leaf, .value = least };
}

struct LeafSamples {
	std::uint32_t leaf;
	int found;
	bool mixed;
};

void sampledFrom(const RunQuery& query, const Face& face, const double* start, LeafSamples& samples) {
	const SurfaceSample sample = projectedOnto(face.program, query.box, start);
	if (!sample.found || samples.mixed) {
		return;
	}
	const SourceReading reading = sourceAt(query, sample.p);
	const double size           = query.box.axes[0].upper - query.box.axes[0].lower;
	if (std::abs(reading.value) > 1e-7 * size) {
		return;
	}
	if (reading.leaf == mixedLeaf) {
		const double outside[3] = { sample.p[0] + 1e-5 * size * sample.gradient[0], sample.p[1] + 1e-5 * size * sample.gradient[1], sample.p[2] + 1e-5 * size * sample.gradient[2] };
		const double inward[3]  = { sample.p[0] - 1e-5 * size * sample.gradient[0], sample.p[1] - 1e-5 * size * sample.gradient[1], sample.p[2] - 1e-5 * size * sample.gradient[2] };
		if (sourceAt(query, outside).value < 0 && sourceAt(query, inward).value < 0) {
			return;
		}
	}
	if (reading.leaf == mixedLeaf || reading.leaf == noLeaf || (samples.found > 0 && reading.leaf != samples.leaf)) {
		samples.mixed = true;
		return;
	}
	samples.leaf = reading.leaf;
	samples.found += 1;
}

std::uint32_t sampledLeaf(const RunQuery& query, const Face& face) {
	const Box& box           = query.box;
	LeafSamples samples      = { .leaf = noLeaf, .found = 0, .mixed = false };
	constexpr int corners[5] = { 8, 0, 3, 5, 6 };
	for (const int k : corners) {
		double start[3];
		for (int axis = 0; axis < 3; ++axis) {
			const double centre = 0.5 * (box.axes[axis].lower + box.axes[axis].upper);
			const double half   = 0.5 * (box.axes[axis].upper - box.axes[axis].lower);
			start[axis]         = k == 8 ? centre : centre + (((k >> axis) & 1) != 0 ? 0.5 : -0.5) * half;
		}
		sampledFrom(query, face, start, samples);
	}
	for (int k = 0; k < 27 && samples.found == 0 && !samples.mixed; ++k) {
		double start[3];
		int rest = k;
		for (int axis = 0; axis < 3; ++axis) {
			start[axis] = box.axes[axis].lower + (1.0 + 2.0 * (rest % 3)) / 6.0 * (box.axes[axis].upper - box.axes[axis].lower);
			rest /= 3;
		}
		sampledFrom(query, face, start, samples);
	}
	return samples.mixed || samples.found == 0 ? noLeaf : samples.leaf;
}

bool sameSurface(const Face& a, const Face& b) {
	double largestA = 0;
	double largestB = 0;
	for (int k = 0; k < 10; ++k) {
		largestA = std::max(largestA, std::abs(a.quadric.coefficients[k]));
		largestB = std::max(largestB, std::abs(b.quadric.coefficients[k]));
	}
	if (!(largestA > 0 && largestB > 0)) {
		return false;
	}
	bool same     = true;
	bool opposite = true;
	for (int k = 0; k < 10; ++k) {
		const double x = a.quadric.coefficients[k] / largestA;
		const double y = b.quadric.coefficients[k] / largestB;
		same           = same && std::abs(x - y) <= 1e-6;
		opposite       = opposite && std::abs(x + y) <= 1e-6;
	}
	return same || opposite;
}

bool duplicated(const std::vector<Face>& faces, std::uint32_t j) {
	for (std::uint32_t k = 0; k < faces.size(); ++k) {
		if (k != j && sameSurface(faces[j], faces[k])) {
			return true;
		}
	}
	return false;
}

std::uint32_t onePaint(const RunQuery& query) {
	std::uint32_t leaf = noLeaf;
	for (const std::shared_ptr<const Program>& program : query.programs) {
		for (const Step& step : program->steps) {
			if (step.leaf != noLeaf && leaf != noLeaf && step.leaf != leaf) {
				return noLeaf;
			}
			leaf = step.leaf != noLeaf ? step.leaf : leaf;
		}
	}
	return leaf;
}

std::uint32_t describedWord(std::uint32_t exact, std::uint32_t leaf) {
	return (exact & 0xFFu) | ((leaf == noLeaf ? 0xFFFFFFu : leaf) << 8);
}

std::uint32_t describedExact(std::uint32_t word) {
	return word & 0xFFu;
}

std::uint32_t describedLeaf(std::uint32_t word) {
	const std::uint32_t leaf = word >> 8;
	return leaf == 0xFFFFFFu ? noLeaf : leaf;
}

std::uint32_t ownersWord(const std::vector<Face>& faces) {
	std::uint32_t owners = 0xFFFFFFFFu;
	for (std::size_t j = 0; j < faces.size(); ++j) {
		const std::uint32_t owner = faces[j].owner < unknownOwner ? faces[j].owner : unknownOwner;
		owners                    = (owners & ~(unknownOwner << (4 * j))) | (owner << (4 * j));
	}
	return owners;
}

bool exactFace(const Face& face, double tolerance) {
	const bool proven = face.proven ? face.bound <= exactShare * tolerance * face.scale : face.ring.has_value();
	return proven && !face.shared && !blends(face.program);
}

std::vector<std::uint32_t> facedWords(const RunQuery& query) {
	if (!facedHitsBuilt) {
		return {};
	}
	const std::vector<Face>& faces         = query.faces.faces;
	const std::uint32_t painted            = onePaint(query);
	std::uint32_t leaves[mostDescriptions] = { noLeaf, noLeaf, noLeaf };
	std::uint32_t exact[mostDescriptions]  = { 0, 0, 0 };
	for (std::uint32_t j = 0; j < faces.size(); ++j) {
		if (!exactFace(faces[j], query.tolerance) || duplicated(faces, j)) {
			continue;
		}
		const std::uint32_t own = painted != noLeaf ? painted : sampledLeaf(query, faces[j]);
		int slot                = -1;
		for (int k = mostDescriptions - 1; k >= 0 && own != noLeaf; --k) {
			slot = leaves[k] == noLeaf || leaves[k] == own ? k : slot;
		}
		if (slot < 0) {
			continue;
		}
		leaves[slot] = own;
		exact[slot] |= 1u << j;
	}
	const std::uint32_t owners       = ownersWord(faces);
	std::vector<std::uint32_t> words = { owners };
	for (int k = 0; k < mostDescriptions && exact[k] != 0; ++k) {
		words.push_back(describedWord(exact[k], leaves[k]));
	}
	if (words.size() == 1 && owners == 0xFFFFFFFFu) {
		return {};
	}
	return words;
}

std::uint32_t keptBits(std::uint32_t mask, std::uint32_t kept) {
	std::uint32_t packed = 0;
	int next             = 0;
	for (std::uint32_t rest = kept; rest != 0; rest &= rest - 1) {
		packed |= ((mask >> std::countr_zero(rest)) & 1u) << next;
		next += 1;
	}
	return packed;
}

std::uint32_t keptOwners(std::uint32_t owners, std::uint32_t kept) {
	std::uint32_t packed = 0xFFFFFFFFu;
	int next             = 0;
	for (std::uint32_t rest = kept; rest != 0; rest &= rest - 1) {
		const std::uint32_t owner = (owners >> (4 * std::countr_zero(rest))) & unknownOwner;
		packed                    = (packed & ~(unknownOwner << (4 * next))) | (owner << (4 * next));
		next += 1;
	}
	return packed;
}

std::vector<std::uint32_t> foldedFacedWords(const std::uint32_t* words, std::uint32_t count, std::uint32_t kept) {
	if (count == 0) {
		return {};
	}
	std::vector<std::uint32_t> folded = { keptOwners(words[0], kept) };
	for (std::uint32_t k = 1; k < count; ++k) {
		folded.push_back(describedWord(keptBits(describedExact(words[k]), kept), describedLeaf(words[k])));
	}
	return folded;
}

FacedFace facedFace(const std::uint32_t* run, std::uint32_t face) {
	const RunHeader header  = runHeader(run[0]);
	const std::uint32_t* at = run + 1 + tableWordCount(header.count);

	FacedFace known = {
		.exact = false,
		.leaf  = noLeaf,
		.owner = unknownOwner,
	};

	if (header.passWords == 0 || face >= 8) {
		return known;
	}
	known.owner = (at[0] >> (4 * face)) & unknownOwner;
	for (std::uint32_t k = 1; k < header.passWords; ++k) {
		if (((describedExact(at[k]) >> face) & 1u) != 0) {
			known.exact = true;
			known.leaf  = describedLeaf(at[k]);
		}
	}
	return known;
}

const float* faceWords(const std::uint32_t* run, std::uint32_t face) {
	const RunHeader header = runHeader(run[0]);
	std::uint32_t at       = firstFaceWord(header);
	for (std::uint32_t k = 0; k < face; ++k) {
		at += faceWordCount(header, k, run + at);
	}
	return reinterpret_cast<const float*>(run + at);
}

std::array<float, 3> ringGradient(const float* ring, const float* u) {
	float v[3];
	for (int k = 0; k < 3; ++k) {
		v[k] = ring[k] + ring[3 + k] * u[0] + ring[6 + k] * u[1] + ring[9 + k] * u[2];
	}
	const float major             = ring[12];
	const float minor             = ring[13];
	const float q                 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + major * major - minor * minor;
	const float m                 = 8.0f * major * major;
	const float dv[3]             = { 4.0f * q * v[0] - m * v[0], 4.0f * q * v[1] - m * v[1], 4.0f * q * v[2] };
	const float sign              = minor < 0.0f ? -1.0f : 1.0f;
	std::array<float, 3> gradient = { 0, 0, 0 };
	for (int j = 0; j < 3; ++j) {
		gradient[std::size_t(j)] = sign * (dv[0] * ring[3 + 3 * j] + dv[1] * ring[4 + 3 * j] + dv[2] * ring[5 + 3 * j]);
	}
	return gradient;
}

std::array<float, 3> facedGradient(const std::uint32_t* run, std::uint32_t face, const float* unit) {
	const float* f = faceWords(run, face);
	const float* u = unit;
	switch (faceKind(runHeader(run[0]), face)) {
		case FaceKind::Plane: return { f[0], f[1], f[2] };
		case FaceKind::Quadric: return { 2.0f * f[0] * u[0] + f[3] * u[1] + f[4] * u[2] + f[6], 2.0f * f[1] * u[1] + f[3] * u[0] + f[5] * u[2] + f[7], 2.0f * f[2] * u[2] + f[4] * u[0] + f[5] * u[1] + f[8] };
		case FaceKind::Ring: return ringGradient(f, u);
		case FaceKind::Patch: break;
	}
	return { 0, 0, 0 };
}

}
