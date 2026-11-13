#include "plugins/feature/dispersion/plugin.h"

namespace kodanuki {

Plugin featureDispersionPlugin() {
	return Plugin {
		.name   = "feature/dispersion",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
