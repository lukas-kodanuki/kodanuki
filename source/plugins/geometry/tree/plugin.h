#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `geometry/tree` as the core lists it: its
 * buildWorld builds the tree of exact faces over the scene
 * (builtGeometry(): the pilot's flight, the build and the memory
 * handed back), keeps it for the run and gives firstHit() as the
 * kernel. It owns the points Detail and Build, and publishes the
 * tree's size as `bytes.tree`.
 */
Plugin geometryTreePlugin();

} // namespace kodanuki
