#include "plugins/build/face_slabs/plugin.h"

namespace kodanuki {

Plugin buildFaceSlabsPlugin() {
	return Plugin {
		.name   = "build/face_slabs",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = noHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
