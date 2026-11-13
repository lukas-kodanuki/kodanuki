#pragma once

#include <cmath>
#include <vector>

namespace kodanuki::math {

/**
 * The ratio of a circle's circumference to its diameter.
 */
constexpr double pi = 3.14159265358979323846;

/**
 * A point or a direction in world space, in double precision.
 *
 * Transport, shading and the lights compute in it. The walk's packet
 * maths keeps its own float types.
 */
struct Vector {
	/**
	 * The x component.
	 */
	double x;
	/**
	 * The y component.
	 */
	double y;
	/**
	 * The z component.
	 */
	double z;
};

/**
 * A point or a direction as it is kept between two steps, in single
 * precision (rule 5): in a ray, a hit, a walk.
 *
 * It is made from a Vector wherever one is stored and turns into one
 * wherever it is read, so every kernel computes in double. Its three
 * floats lie as a GPU buffer holds them. Single precision is enough
 * for scenes within about 1e5 units of the origin.
 */
struct FloatVector {
	/**
	 * The x component.
	 */
	float x;
	/**
	 * The y component.
	 */
	float y;
	/**
	 * The z component.
	 */
	float z;
	/**
	 * Leave the components unset, as a plain struct's are.
	 */
	FloatVector() = default;

	/**
	 * Round a vector to single precision.
	 *
	 * @param wide The vector.
	 */
	FloatVector(const Vector& wide) : x(float(wide.x)), y(float(wide.y)), z(float(wide.z)) {
	}

	/**
	 * Round three components to single precision.
	 *
	 * @param x The x component.
	 * @param y The y component.
	 * @param z The z component.
	 */
	FloatVector(double x, double y, double z) : x(float(x)), y(float(y)), z(float(z)) {
	}

	/**
	 * Return the vector in double precision.
	 */
	operator Vector() const {
		return { x, y, z };
	}
};

/**
 * Three unit vectors at right angles, a frame around a normal.
 */
struct Basis {
	/**
	 * The first axis across the normal.
	 */
	Vector tangent;
	/**
	 * The second axis across the normal.
	 */
	Vector bitangent;
	/**
	 * The normal the frame is built around.
	 */
	Vector normal;
};

/**
 * Add two vectors component by component.
 *
 * @param a The first vector.
 * @param b The second vector.
 */
inline Vector operator+(const Vector& a, const Vector& b) {
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}

/**
 * Subtract one vector from another component by component.
 *
 * @param a The vector subtracted from.
 * @param b The vector subtracted.
 */
inline Vector operator-(const Vector& a, const Vector& b) {
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}

/**
 * Turn a vector round.
 *
 * @param a The vector.
 */
inline Vector operator-(const Vector& a) {
	return { -a.x, -a.y, -a.z };
}

/**
 * Scale a vector by a number.
 *
 * @param a The vector.
 * @param s The number.
 */
inline Vector operator*(const Vector& a, double s) {
	return { a.x * s, a.y * s, a.z * s };
}

/**
 * Divide a vector by a number that is not zero.
 *
 * @param a The vector.
 * @param s The number.
 */
inline Vector operator/(const Vector& a, double s) {
	return { a.x / s, a.y / s, a.z / s };
}

/**
 * Return the dot product of two vectors.
 *
 * @param a The first vector.
 * @param b The second vector.
 */
inline double dot(const Vector& a, const Vector& b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

/**
 * Return the cross product of two vectors.
 *
 * @param a The first vector.
 * @param b The second vector.
 */
inline Vector cross(const Vector& a, const Vector& b) {
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

/**
 * Return the length of a vector.
 *
 * @param a The vector.
 */
inline double length(const Vector& a) {
	return std::sqrt(dot(a, a));
}

/**
 * Return a vector scaled to unit length.
 *
 * The zero vector comes back unchanged.
 *
 * @param a The vector.
 */
inline Vector normalized(const Vector& a) {
	const double size = length(a);
	return size > 0 ? a / size : a;
}

/**
 * Return one component of a vector by its axis: 0 for x, 1 for y and 2
 * for z.
 *
 * @param a The vector.
 * @param axis The axis.
 */
double component(const Vector& a, int axis);

/**
 * Return the vector the first three floats of a list hold, as the scene
 * language gives points and directions.
 *
 * @param from The list, at least three long.
 */
Vector vectorFromFloats(const std::vector<float>& from);

/**
 * Return the area of the parallelogram two edges span.
 *
 * @param a The first edge.
 * @param b The second edge.
 */
double parallelogramArea(const Vector& a, const Vector& b);

/**
 * Return a frame around a unit normal.
 *
 * The tangent follows Duff et al.'s branchless construction, so the
 * same normal always gives the same frame.
 *
 * @param normal The unit normal.
 */
Basis basisAround(const Vector& normal);

/**
 * Return a world direction in a frame's coordinates.
 *
 * @param basis The frame.
 * @param v The direction in world space.
 */
Vector toLocal(const Basis& basis, const Vector& v);

/**
 * Return a direction given in a frame's coordinates in world space.
 *
 * @param basis The frame.
 * @param v The direction in the frame.
 */
Vector toWorld(const Basis& basis, const Vector& v);

/**
 * Return a unit direction mirrored about a unit normal.
 *
 * The direction points away from the surface, and so does the result.
 *
 * @param v The direction.
 * @param normal The normal.
 */
Vector mirrored(const Vector& v, const Vector& normal);

/**
 * Return a normal turned to face against a heading.
 *
 * It comes back flipped where it points along the heading.
 *
 * @param normal The normal.
 * @param heading The direction a ray travels in.
 */
Vector facingAgainst(const Vector& normal, const Vector& heading);

}
