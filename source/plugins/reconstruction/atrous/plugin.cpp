#include "plugins/reconstruction/atrous/plugin.h"

#include "core/reconstruction.h"
#include "plugins/reconstruction/atrous/atrous.h"

#include <cstddef>
#include <utility>

namespace kodanuki {

bool temporalShown(const TemporalState& temporal, const ShownFrame& frame) {
	const TemporalGrid grid = temporalGrid(frame);
	return temporal.width == grid.width && temporal.height == grid.height && temporal.pixels.size() == std::size_t(grid.width) * std::size_t(grid.height);
}

Image denoisedReconstructed(States& states, Image shown, const ReconstructionQuery& query) {
	const TemporalState& temporal = temporalState(states);
	if (!temporalShown(temporal, query.frame)) {
		return shown;
	}
	DenoiseState& state = denoiseState(states);
	Denoised denoised   = denoisedFrame(std::move(state), temporal, query.frame);
	state               = std::move(denoised.state);
	return std::move(denoised.image);
}

Hooks atrousHooks() {
	Hooks hooks            = noHooks();
	hooks.reconstructFrame = denoisedReconstructed;
	return hooks;
}

Plugin reconstructionAtrousPlugin() {
	return Plugin {
		.name   = "reconstruction/atrous",
		.point  = Point::Reconstruction,
		.effect = Effect::Biased,
		.hooks  = atrousHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
