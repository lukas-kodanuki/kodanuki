#include "math/vector.h"

#include <cmath>

namespace kodanuki::math {

double component(const Vector& a, int axis) {
	switch (axis) {
		case 0: return a.x;
		case 1: return a.y;
	}
	return a.z;
}

Vector vectorFromFloats(const std::vector<float>& from) {
	return { from[0], from[1], from[2] };
}

double parallelogramArea(const Vector& a, const Vector& b) {
	return length(cross(a, b));
}

Basis basisAround(const Vector& normal) {
	const double side = std::copysign(1.0, normal.z);
	const double a    = -1.0 / (side + normal.z);
	const double b    = normal.x * normal.y * a;

	const Basis basis = {
		.tangent   = { 1.0 + side * normal.x * normal.x * a, side * b, -side * normal.x },
		.bitangent = { b, side + normal.y * normal.y * a, -normal.y },
		.normal    = normal,
	};

	return basis;
}

Vector toLocal(const Basis& basis, const Vector& v) {
	return { dot(v, basis.tangent), dot(v, basis.bitangent), dot(v, basis.normal) };
}

Vector toWorld(const Basis& basis, const Vector& v) {
	return basis.tangent * v.x + basis.bitangent * v.y + basis.normal * v.z;
}

Vector mirrored(const Vector& v, const Vector& normal) {
	return normal * (2 * dot(v, normal)) - v;
}

Vector facingAgainst(const Vector& normal, const Vector& heading) {
	return dot(normal, heading) > 0 ? -normal : normal;
}

}
