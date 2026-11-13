#include "plugins/feature/scattering/plugin.h"

namespace kodanuki {

Plugin featureScatteringPlugin() {
	return Plugin {
		.name   = "feature/scattering",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
