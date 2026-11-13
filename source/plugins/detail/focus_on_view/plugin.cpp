#include "plugins/detail/focus_on_view/plugin.h"

namespace kodanuki {

Plugin detailFocusOnViewPlugin() {
	return Plugin {
		.name   = "detail/focus_on_view",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
