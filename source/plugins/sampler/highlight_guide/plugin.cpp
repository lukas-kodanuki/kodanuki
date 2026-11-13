#include "plugins/sampler/highlight_guide/plugin.h"

#include "core/render.h"
#include "plugins/sampler/highlight_guide/survey.h"
#include "plugins/transport/paths/paths.h"

#include <utility>

namespace kodanuki {

bool keepsGuides(const FrameStep& step) {
	return step.keepsGuides && step.change == Change::Moved;
}

void guideSurveyPrepared(Renderer& renderer) {
	if (keepsGuides(renderer.step)) {
		return;
	}

	const GuideFrame frame = {
		.tracing = tracingOf(renderer.drawing.transported.get()),
		.tunings = defaultGuideTunings(),
		.threads = renderer.options.choices.threads,
	};

	HighlightSurvey survey                        = surveyOf(frame);
	HighlightGuides guides                        = pixelGuides(frame, survey);
	guides.survey                                 = std::move(survey);
	highlightGuidesState(renderer.history.states) = std::move(guides);
}

Hooks guideSurveyPreparedHooks() {
	Hooks hooks        = noHooks();
	hooks.prepareFrame = guideSurveyPrepared;
	return hooks;
}

Plugin samplerHighlightGuidePlugin() {
	return Plugin {
		.name   = "sampler/highlight_guide",
		.point  = Point::Owned,
		.effect = Effect::Exact,
		.hooks  = guideSurveyPreparedHooks(),
		.reads  = everyRead(),
		.after  = {},
	};
}

} // namespace kodanuki
