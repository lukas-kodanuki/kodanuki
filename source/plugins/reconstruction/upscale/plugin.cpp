#include "plugins/reconstruction/upscale/plugin.h"

#include "core/reconstruction.h"
#include "plugins/reconstruction/upscale/upscale.h"

#include <utility>

namespace kodanuki {

Image upscaledReconstructed(States& states, Image shown, const ReconstructionQuery& query) {
	UpscaleState& state = upscaleState(states);
	Upscaled upscaled   = upscaledFrame(std::move(state), shown, query.frame);
	state               = std::move(upscaled.state);
	return std::move(upscaled.image);
}

Hooks upscaleHooks() {
	Hooks hooks            = noHooks();
	hooks.reconstructFrame = upscaledReconstructed;
	return hooks;
}

Plugin reconstructionUpscalePlugin() {
	return Plugin {
		.name   = "reconstruction/upscale",
		.point  = Point::Reconstruction,
		.effect = Effect::Biased,
		.hooks  = upscaleHooks(),
		.reads  = Reads::Surfaces,
		.after  = {},
	};
}

} // namespace kodanuki
