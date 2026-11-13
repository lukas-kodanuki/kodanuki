#include "plugins/strategy/join/plugin.h"

#include "core/render.h"
#include "plugins/strategy/join/pass.h"
#include "plugins/transport/paths/paths.h"

#include <utility>

namespace kodanuki {

void lightPassPrepared(Renderer& renderer) {
	CarriedLight light                         = carriedLight(renderer);
	carriedLightState(renderer.history.states) = std::move(light);
}

Colour joinedLight(States& states, std::size_t pixel) {
	return carriedMean(carriedLightState(states), pixel);
}

Colour joinedSpread(States& states, std::size_t pixel) {
	return carriedSpread(carriedLightState(states), pixel);
}

Hooks joinHooks() {
	Hooks hooks         = noHooks();
	hooks.prepareFrame  = lightPassPrepared;
	hooks.broughtLight  = joinedLight;
	hooks.broughtSpread = joinedSpread;
	return hooks;
}

Plugin strategyJoinPlugin() {
	return Plugin {
		.name   = "strategy/join",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = joinHooks(),
		.reads  = everyRead(),
		.after  = { "sampler/sky_map", "sampler/glowing_surfaces", "sampler/toward_mirrors", "sampler/highlight_guide" },
	};
}

} // namespace kodanuki
