#include "math/random.h"

#include <algorithm>
#include <cmath>

namespace kodanuki::math {

std::uint64_t mixedBits(std::uint64_t value) {
	value += 0x9E3779B97F4A7C15ULL;
	value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
	value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
	return value ^ (value >> 31);
}

double unitOf(std::uint64_t bits) {
	return double(bits >> 11) * 0x1p-53;
}

double hashedNumber(const Key& key) {
	return unitOf(mixedBits(key.stream + mixedBits(key.index + mixedBits(key.coordinate))));
}

Vector cosineHemisphere(const Basis& basis, const UnitPair& pair) {
	const double reach  = std::sqrt(pair.u);
	const double angle  = 2 * pi * pair.v;
	const double height = std::sqrt(std::max(1 - pair.u, 0.0));
	return toWorld(basis, { reach * std::cos(angle), reach * std::sin(angle), height });
}

double cosineDensity(const Vector& normal, const Vector& direction) {
	return std::max(dot(normal, direction), 0.0) / pi;
}

Vector uniformSphere(const UnitPair& pair) {
	const double height = 1 - 2 * pair.u;
	const double ring   = std::sqrt(std::max(1 - height * height, 0.0));
	const double angle  = 2 * pi * pair.v;
	return { ring * std::cos(angle), ring * std::sin(angle), height };
}

Vector uniformCone(const Cone& cone, const UnitPair& pair) {
	const double height = 1 - pair.u * (1 - cone.cosine);
	const double ring   = std::sqrt(std::max(1 - height * height, 0.0));
	const double angle  = 2 * pi * pair.v;
	return toWorld(cone.axis, { ring * std::cos(angle), ring * std::sin(angle), height });
}

UnitPair concentricDisc(const UnitPair& pair) {
	const double a = 2 * pair.u - 1;
	const double b = 2 * pair.v - 1;
	if (a == 0 && b == 0) {
		return { 0, 0 };
	}
	const bool wide     = std::abs(a) > std::abs(b);
	const double radius = wide ? a : b;
	const double angle  = wide ? pi / 4 * (b / a) : pi / 2 - pi / 4 * (a / b);
	return { radius * std::cos(angle), radius * std::sin(angle) };
}

}
