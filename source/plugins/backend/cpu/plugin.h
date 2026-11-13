#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `backend/cpu` as the core lists it: it opens one
 * worker per thread (--threads), hosts the workers of every chosen
 * backend on threads of its own, and runs a worker's step by calling
 * the plugin's C++ kernel; it is the fallback of every other backend.
 */
Plugin backendCpuPlugin();

} // namespace kodanuki
