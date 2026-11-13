#include "plugins/build/cut_long_programs/plugin.h"

namespace kodanuki {

Plugin buildCutLongProgramsPlugin() {
	return Plugin {
		.name   = "build/cut_long_programs",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
