#include "plugins/sampler/each_lamp_first/plugin.h"

namespace kodanuki {

Plugin samplerEachLampFirstPlugin() {
	return Plugin {
		.name   = "sampler/each_lamp_first",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
