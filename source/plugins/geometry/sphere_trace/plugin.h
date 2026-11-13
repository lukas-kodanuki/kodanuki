#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `geometry/sphere_trace` as the core lists it: the
 * reference of the Geometry point. Its buildWorld keeps the scene's
 * object programs and the region (`--region`, else the cube of the
 * render's reach), and its kernel sphere-traces the scene's field
 * (sphereTraced()). It builds no tree and no faces, so it runs with
 * `execution/single` and without the plugins that read the tree.
 */
Plugin geometrySphereTracePlugin();

} // namespace kodanuki
