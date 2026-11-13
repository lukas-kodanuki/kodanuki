#include "plugins/stopping/confidence_interval/plugin.h"

#include "plugins/stopping/confidence_interval/confidence_interval.h"

namespace kodanuki {

Hooks confidenceHooks() {
	Hooks hooks           = noHooks();
	hooks.trialSamples    = confidenceTrial;
	hooks.trialSplit      = confidenceTrialSplit;
	hooks.wantedSamples   = confidenceWanted;
	hooks.allottedSamples = confidenceAllotted;
	hooks.allottedSplit   = confidenceSplit;
	return hooks;
}

Plugin stoppingConfidenceIntervalPlugin() {
	return Plugin {
		.name   = "stopping/confidence_interval",
		.point  = Point::Stopping,
		.effect = Effect::Consistent,
		.hooks  = confidenceHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
