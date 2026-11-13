#include "plugins/sampler/sky_map/plugin.h"

#include "core/render.h"
#include "plugins/sampler/sky_map/sky_map.h"
#include "plugins/transport/paths/paths.h"

#include <utility>

namespace kodanuki {

math::Vector regionMiddle(const Region& region) {
	return { region.lower[0] + 0.5 * region.size[0], region.lower[1] + 0.5 * region.size[1], region.lower[2] + 0.5 * region.size[2] };
}

void chartedSkyPrepared(Renderer& renderer) {
	const Tracing& tracing = tracingOf(renderer.drawing.transported.get());

	const SkyChartQuery chart = {
		.lights   = tracing.lights,
		.features = tracing.features,
		.middle   = regionMiddle(tracing.geometry.region),
	};

	SkyChart charted                       = chartedSky(chart);
	skyChartState(renderer.history.states) = std::move(charted);
}

Hooks chartedSkyPreparedHooks() {
	Hooks hooks        = noHooks();
	hooks.prepareFrame = chartedSkyPrepared;
	return hooks;
}

Plugin samplerSkyMapPlugin() {
	return Plugin {
		.name   = "sampler/sky_map",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = chartedSkyPreparedHooks(),
		.reads  = joinedReads(Reads::Lights, Reads::Shapes),
		.after  = {},
	};
}

} // namespace kodanuki
