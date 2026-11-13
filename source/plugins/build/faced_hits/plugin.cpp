#include "plugins/build/faced_hits/plugin.h"

namespace kodanuki {

Plugin buildFacedHitsPlugin() {
	return Plugin {
		.name   = "build/faced_hits",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
