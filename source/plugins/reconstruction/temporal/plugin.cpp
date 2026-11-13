#include "plugins/reconstruction/temporal/plugin.h"

#include "core/reconstruction.h"
#include "plugins/reconstruction/temporal/temporal.h"

#include <utility>

namespace kodanuki {

Image temporalReconstructed(States& states, Image, const ReconstructionQuery& query) {
	TemporalState& state = temporalState(states);
	Temporal temporal    = temporalFrame(std::move(state), query.frame);
	state                = std::move(temporal.state);
	return std::move(temporal.image);
}

Hooks temporalHooks() {
	Hooks hooks            = noHooks();
	hooks.reconstructFrame = temporalReconstructed;
	return hooks;
}

Plugin reconstructionTemporalPlugin() {
	return Plugin {
		.name   = "reconstruction/temporal",
		.point  = Point::Reconstruction,
		.effect = Effect::Biased,
		.hooks  = temporalHooks(),
		.reads  = joinedReads(Reads::Surfaces, Reads::Records),
		.after  = {},
	};
}

} // namespace kodanuki
