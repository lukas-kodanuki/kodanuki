#pragma once

#include "core/plugin.h"

namespace kodanuki {

/**
 * Return the plugin `source/scene` as the core lists it: the frames of
 * the scene file, each at its time and through the scene's camera at
 * that time (sceneAt()).
 *
 * A still is one frame at time 0, or at --time where it is given, of a
 * file with a clip or without. A clip gives its frames one after another,
 * frame k at k / rate, and a frame that reaches one of the clip's cuts is
 * a Cut. Where a Sink plays the clip (Hooks.playedClip), the run is
 * interactive: every frame takes the clip's frame the Sink plays, the
 * clip starts again past its last frame, and that wrap is a Cut too; a
 * still then stands for as long as the run lasts.
 */
Plugin sourceScenePlugin();

} // namespace kodanuki
