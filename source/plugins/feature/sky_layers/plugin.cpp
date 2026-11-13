#include "plugins/feature/sky_layers/plugin.h"

namespace kodanuki {

Plugin featureSkyLayersPlugin() {
	return Plugin {
		.name   = "feature/sky_layers",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
