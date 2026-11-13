#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `execution/single` as the core lists it: its
 * extendRays traces every waiting ray of a pool on its own, by the
 * scalar intersect().
 */
Plugin executionSinglePlugin();

} // namespace kodanuki
