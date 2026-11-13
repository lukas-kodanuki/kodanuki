#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `transport/paths` as the core lists it: the path
 * engine. Its walks arrive at their hits, connect to the lights and
 * scatter; it owns the Sampler, Feature and Strategy points and holds
 * the materials and the lights.
 */
Plugin transportPathsPlugin();

} // namespace kodanuki
