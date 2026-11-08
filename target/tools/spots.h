#pragma once

#include "sdf/scene.h"
#include "tree/cut.h"

#include <vector>

namespace kodanuki {

/**
 * A point in world space where a tool measures the scene.
 */
using Spot = math::Vector;

/**
 * Return the scene's distance at a point.
 *
 * @param scene The scene.
 * @param p The point.
 */
double distanceAt(const Scene& scene, const Spot& p);

/**
 * Return the point a distance along a ray.
 *
 * @param from Where the ray starts.
 * @param direction The ray's unit direction.
 * @param t How far along the ray.
 */
Spot along(const Spot& from, const Spot& direction, double t);

/**
 * Return the unit direction from one point toward another.
 *
 * @param from The first point.
 * @param to The point looked at.
 */
Spot unitToward(const Spot& from, const Spot& to);

/**
 * Return points on the scene's surface, where rays from the eye toward
 * random targets first meet it.
 *
 * The targets lie in x and y within 4 of the origin and in z between 0
 * and the given height, drawn from a fixed seed, so every run measures
 * the same points.
 *
 * @param file The scene and its camera.
 * @param count How many points.
 * @param height The highest a target lies.
 */
std::vector<Spot> surfaceSpots(const SceneFile& file, int count, double height);

/**
 * Return the span of the grid cell of a size that holds a coordinate.
 *
 * @param coordinate The coordinate.
 * @param size The cell's size.
 */
math::Interval cellSpan(double coordinate, double size);

/**
 * Return the grid cell of a size that holds a point.
 *
 * @param p The point.
 * @param size The cell's size.
 */
Box cellAround(const Spot& p, double size);

}
