#include "plugins/feature/absorption/plugin.h"

namespace kodanuki {

Plugin featureAbsorptionPlugin() {
	return Plugin {
		.name   = "feature/absorption",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
