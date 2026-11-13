#include "plugins/build/shared_subtrees/plugin.h"

namespace kodanuki {

Plugin buildSharedSubtreesPlugin() {
	return Plugin {
		.name   = "build/shared_subtrees",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
