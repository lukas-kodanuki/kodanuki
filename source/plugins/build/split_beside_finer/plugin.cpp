#include "plugins/build/split_beside_finer/plugin.h"

namespace kodanuki {

Plugin buildSplitBesideFinerPlugin() {
	return Plugin {
		.name   = "build/split_beside_finer",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
