#include "plugins/sampler/sobol/plugin.h"

namespace kodanuki {

Plugin samplerSobolPlugin() {
	return Plugin {
		.name   = "sampler/sobol",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
