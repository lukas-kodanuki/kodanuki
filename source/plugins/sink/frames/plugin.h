#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `sink/frames` as the core lists it: each frame to
 * frame-NNNN.ppm and .pfm in --out, and its line: the seconds of its
 * steps, its time, its samples a pixel, the seconds the scene's
 * evaluation and the world's build took, and the noise.
 */
Plugin sinkFramesPlugin();

} // namespace kodanuki
