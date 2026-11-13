#include "plugins/build/shared_coefficients/plugin.h"

namespace kodanuki {

Plugin buildSharedCoefficientsPlugin() {
	return Plugin {
		.name   = "build/shared_coefficients",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
