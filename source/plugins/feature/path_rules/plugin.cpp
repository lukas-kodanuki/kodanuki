#include "plugins/feature/path_rules/plugin.h"

namespace kodanuki {

Plugin featurePathRulesPlugin() {
	return Plugin {
		.name   = "feature/path_rules",
		.point  = Point::Owned,
		.effect = Effect::Biased,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
