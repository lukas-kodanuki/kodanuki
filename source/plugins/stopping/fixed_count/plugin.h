#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `stopping/fixed_count` as the core lists it: every
 * pixel draws the scene's sample count, in one round and without a
 * trial.
 */
Plugin stoppingFixedCountPlugin();

} // namespace kodanuki
