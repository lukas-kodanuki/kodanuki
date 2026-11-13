#include "plugins/sampler/toward_mirrors/plugin.h"

#include "core/render.h"
#include "plugins/sampler/toward_mirrors/toward_mirrors.h"
#include "plugins/transport/paths/paths.h"

#include <string>
#include <utility>
#include <vector>

namespace kodanuki {

void mirrorBallsPrepared(Renderer& renderer) {
	const Tracing& tracing                    = tracingOf(renderer.drawing.transported.get());
	MirrorBalls balls                         = relayingBalls(tracing.geometry, tracing.looks, tracing.materials);
	mirrorBallsState(renderer.history.states) = std::move(balls);
}

std::vector<std::string> mirrorsNeeded() {
	return { "geometry/tree" };
}

Hooks mirrorBallsPreparedHooks() {
	Hooks hooks        = noHooks();
	hooks.prepareFrame = mirrorBallsPrepared;
	hooks.needed       = mirrorsNeeded;
	return hooks;
}

Plugin samplerTowardMirrorsPlugin() {
	return Plugin {
		.name   = "sampler/toward_mirrors",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = mirrorBallsPreparedHooks(),
		.reads  = joinedReads(Reads::Shapes, Reads::Looks),
		.after  = {},
	};
}

} // namespace kodanuki
