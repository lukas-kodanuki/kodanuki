#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `execution/packets` as the core lists it: its
 * extendRays sorts the waiting rays of a pool into queues by kind and
 * octant and traces them eight at a time by the packet walk. The
 * packet walk reads the tree itself, so it needs `geometry/tree`
 * (Hooks.needed) and no kernel of the Geometry point is called per
 * packet.
 */
Plugin executionPacketsPlugin();

} // namespace kodanuki
