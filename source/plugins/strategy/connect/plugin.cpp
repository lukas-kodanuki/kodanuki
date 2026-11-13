#include "plugins/strategy/connect/plugin.h"

namespace kodanuki {

Plugin strategyConnectPlugin() {
	return Plugin {
		.name   = "strategy/connect",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
