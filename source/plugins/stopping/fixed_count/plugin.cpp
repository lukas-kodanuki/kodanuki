#include "plugins/stopping/fixed_count/plugin.h"

#include "core/stopping.h"

namespace kodanuki {

int fixedTrial(const Stopping&) {
	return 0;
}

int fixedAllotted(const Stopping& stopping, const CountQuery&) {
	return stopping.samples;
}

Hooks fixedHooks() {
	Hooks hooks           = noHooks();
	hooks.trialSamples    = fixedTrial;
	hooks.allottedSamples = fixedAllotted;
	return hooks;
}

Plugin stoppingFixedCountPlugin() {
	return Plugin {
		.name   = "stopping/fixed_count",
		.point  = Point::Stopping,
		.effect = Effect::Exact,
		.hooks  = fixedHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
