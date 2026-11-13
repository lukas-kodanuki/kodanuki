#include "plugins/geometry/sphere_trace/plugin.h"

#include "core/render.h"
#include "plugins/geometry/sphere_trace/sphere_trace.h"

namespace kodanuki {

Geometry sphereBuilt(Renderer& renderer) {
	const Choices& choices = renderer.options.choices;
	return sphereGeometry(renderer.file.scene, choices.framed ? choices.region : reachRegion(renderer.file.shot));
}

Plugin geometrySphereTracePlugin() {
	Hooks hooks      = noHooks();
	hooks.buildWorld = sphereBuilt;
	return Plugin {
		.name   = "geometry/sphere_trace",
		.point  = Point::Geometry,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
