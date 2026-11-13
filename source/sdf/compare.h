#pragma once

#include "sdf/scene.h"

namespace kodanuki {

/**
 * Return whether two scenes hold the same shapes: the same programs,
 * arguments, leaves, nodes and objects, each field equal.
 *
 * @param a One scene.
 * @param b The other.
 */
bool sameShapes(const Scene& a, const Scene& b);

/**
 * Return whether two scenes look the same: the same materials, coats
 * and groups, each field equal.
 *
 * @param a One scene's looks.
 * @param b The other's.
 */
bool sameLooks(const Looks& a, const Looks& b);

/**
 * Return whether two scenes are lit the same: the same lights, sky,
 * clouds and stars, each field equal.
 *
 * @param a One scene's lighting.
 * @param b The other's.
 */
bool sameLighting(const Lighting& a, const Lighting& b);

/**
 * Return whether two shots draw alike: the same render settings and the
 * same clip. The camera and the grade are left out.
 *
 * @param a One shot.
 * @param b The other.
 */
bool sameSettings(const Shot& a, const Shot& b);

}
