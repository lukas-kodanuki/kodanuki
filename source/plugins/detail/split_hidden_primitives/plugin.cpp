#include "plugins/detail/split_hidden_primitives/plugin.h"

namespace kodanuki {

Plugin detailSplitHiddenPrimitivesPlugin() {
	return Plugin {
		.name   = "detail/split_hidden_primitives",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
