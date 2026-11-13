#include "plugins/reconstruction/upsample/plugin.h"

#include "core/reconstruction.h"
#include "plugins/reconstruction/upsample/upsample.h"

namespace kodanuki {

Image upsampledReconstructed(States&, Image shown, const ReconstructionQuery& query) {
	return upsampledImage(shown, query.frame);
}

Hooks upsampleHooks() {
	Hooks hooks            = noHooks();
	hooks.reconstructFrame = upsampledReconstructed;
	return hooks;
}

Plugin reconstructionUpsamplePlugin() {
	return Plugin {
		.name   = "reconstruction/upsample",
		.point  = Point::Reconstruction,
		.effect = Effect::Biased,
		.hooks  = upsampleHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
