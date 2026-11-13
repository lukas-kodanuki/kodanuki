#include "plugins/feature/clouds/plugin.h"

namespace kodanuki {

Plugin featureCloudsPlugin() {
	return Plugin {
		.name   = "feature/clouds",
		.point  = Point::Owned,
		.effect = Effect::Biased,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
