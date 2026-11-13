#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `sampler/toward_mirrors` as the core lists it. Its
 * balls are gathered from the tree's leaves, so it needs
 * `geometry/tree` (Hooks.needed).
 */
Plugin samplerTowardMirrorsPlugin();

} // namespace kodanuki
