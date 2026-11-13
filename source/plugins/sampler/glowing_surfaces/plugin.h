#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `sampler/glowing_surfaces` as the core lists it.
 * Its cells are the tree's surface leaves, so it needs `geometry/tree`
 * (Hooks.needed).
 */
Plugin samplerGlowingSurfacesPlugin();

} // namespace kodanuki
