#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `source/flight` as the core lists it: the camera
 * the person flies in the window, on top of the scene's, which stays
 * the frame's until the person first moves it; it rests once the
 * samples are reached, until a wish or the clip's next frame, reloads
 * the scene at the frame's time (R), comparing it as a frame of the
 * clip does, and quits. It runs after `source/scene`.
 */
Plugin sourceFlightPlugin();

} // namespace kodanuki
