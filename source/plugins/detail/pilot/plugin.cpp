#include "plugins/detail/pilot/plugin.h"

namespace kodanuki {

Plugin detailPilotPlugin() {
	return Plugin {
		.name   = "detail/pilot",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
