#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `reconstruction/mean` as the core lists it: each
 * drawn pixel's estimate's mean plus what the preparations brought it,
 * at the drawn size. Its state is the history's estimates
 * (History.estimates), which the core continues where nothing changes,
 * so on a still camera at a factor of 1 it shows the image of a still
 * frame of as many samples.
 */
Plugin reconstructionMeanPlugin();

} // namespace kodanuki
