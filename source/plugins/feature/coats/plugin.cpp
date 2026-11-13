#include "plugins/feature/coats/plugin.h"

namespace kodanuki {

Plugin featureCoatsPlugin() {
	return Plugin {
		.name   = "feature/coats",
		.point  = Point::Owned,
		.effect = Effect::Biased,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
