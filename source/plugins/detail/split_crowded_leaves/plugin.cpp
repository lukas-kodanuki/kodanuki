#include "plugins/detail/split_crowded_leaves/plugin.h"

namespace kodanuki {

Plugin detailSplitCrowdedLeavesPlugin() {
	return Plugin {
		.name   = "detail/split_crowded_leaves",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
