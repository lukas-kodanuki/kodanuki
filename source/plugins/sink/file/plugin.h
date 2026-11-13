#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `sink/file` as the core lists it: the still to
 * --out (.ppm) and --pfm, and its summary line.
 */
Plugin sinkFilePlugin();

} // namespace kodanuki
