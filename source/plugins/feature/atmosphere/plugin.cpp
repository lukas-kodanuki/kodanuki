#include "plugins/feature/atmosphere/plugin.h"

namespace kodanuki {

Plugin featureAtmospherePlugin() {
	return Plugin {
		.name   = "feature/atmosphere",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
