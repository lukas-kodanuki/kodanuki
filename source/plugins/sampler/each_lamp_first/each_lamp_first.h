#pragma once

#include "plugins/transport/paths/sampler.h"

namespace kodanuki {

/**
 * Return Own at the first vertex for a lamp while fewer than four lights
 * have offered anything on their own ray, Shared otherwise.
 *
 * A light's aim density is the same either way, so no weight changes.
 *
 * @param query The light, the depth, the lines owned and the lights.
 */
Selection eachLampFirst(const SelectionQuery& query);

}
