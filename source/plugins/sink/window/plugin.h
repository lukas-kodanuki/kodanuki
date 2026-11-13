#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `sink/window` as the core lists it: each frame
 * graded and shown in the window, which keeps the main thread.
 */
Plugin sinkWindowPlugin();

} // namespace kodanuki
