#include "frame/preparation.h"

#include "extras/sampler_correct_highlight_guide/survey.h"
#include "extras/strategy_correct_join/join.h"

#include <utility>

namespace kodanuki {

constexpr int glowGridDepth = 6;

math::Vector regionMiddle(const Region& region) {
	return { region.lower[0] + 0.5 * region.size[0], region.lower[1] + 0.5 * region.size[1], region.lower[2] + 0.5 * region.size[2] };
}

SkyChartQuery chartQueryOf(const Tracing& tracing) {
	return SkyChartQuery {
		.lights   = tracing.lights,
		.features = tracing.features,
		.middle   = regionMiddle(tracing.geometry.tree.region),
	};
}

HighlightGuides surveyedGuides(const PreparationQuery& query) {
	const GuideFrame frame = {
		.tracing = query.tracing,
		.tunings = defaultGuideTunings(),
		.threads = query.threads,
	};

	HighlightSurvey survey = surveyOf(frame);
	HighlightGuides guides = pixelGuides(frame, survey);
	guides.survey          = std::move(survey);
	return guides;
}

void passedLight(const PreparationQuery& query, Prepared& prepared) {
	const Tracing& given        = query.tracing;
	const Strategies strategies = withLightShares(prepared.strategies, given);

	const Tracing tracing = {
		.geometry   = given.geometry,
		.scene      = given.scene,
		.looks      = given.looks,
		.materials  = given.materials,
		.lights     = given.lights,
		.lens       = given.lens,
		.sampling   = given.sampling,
		.strategies = strategies,
		.features   = given.features,
		.limits     = given.limits,
		.seed       = given.seed,
		.film       = given.film,
		.recording  = given.recording,
	};

	const LightPassQuery pass = {
		.tracing   = tracing,
		.stopping  = query.stopping,
		.execution = query.execution,
		.threads   = query.threads,
	};

	prepared.light      = carriedLight(pass);
	prepared.strategies = strategies;
}

void preparedBy(PreparationMember member, const PreparationQuery& query, Prepared& prepared) {
	const Tracing& tracing = query.tracing;
	switch (member) {
		case PreparationMember::SkyChart: prepared.sampling.sky = chartedSky(chartQueryOf(tracing)); break;
		case PreparationMember::GlowGrid: prepared.sampling.glows = gatheredGlowGrid(tracing.geometry, tracing.looks, glowGridDepth); break;
		case PreparationMember::MirrorBalls: prepared.sampling.mirrors = relayingBalls(tracing.geometry, tracing.looks, tracing.materials); break;
		case PreparationMember::GuideSurvey: prepared.sampling.guides = surveyedGuides(query); break;
		case PreparationMember::LightPass: passedLight(query, prepared); break;
	}
}

Colour preparedLight(const Prepared& prepared, std::size_t pixel) {
	return carriedMean(prepared.light, pixel);
}

Colour preparedSpread(const Prepared& prepared, std::size_t pixel) {
	return carriedSpread(prepared.light, pixel);
}

Prepared corePrepared() {
	return Prepared {
		.sampling   = coreSampling(),
		.strategies = coreStrategies(),
		.light      = noCarriedLight(),
	};
}

Prepared defaultPrepared() {
	Strategies strategies = coreStrategies();
	strategies.members.push_back(StrategyMember::Join);
	return Prepared {
		.sampling   = defaultSampling(),
		.strategies = strategies,
		.light      = noCarriedLight(),
	};
}

PreparationReads preparationReads(PreparationMember member) {
	switch (member) {
		case PreparationMember::SkyChart: return PreparationReads::Load;
		case PreparationMember::GlowGrid: return PreparationReads::Load;
		case PreparationMember::MirrorBalls: return PreparationReads::Load;
		case PreparationMember::GuideSurvey: return PreparationReads::Camera;
		case PreparationMember::LightPass: return PreparationReads::Camera;
	}

	return PreparationReads::Camera;
}

std::vector<PreparationMember> defaultPreparations() {
	return { PreparationMember::SkyChart, PreparationMember::GlowGrid, PreparationMember::MirrorBalls, PreparationMember::GuideSurvey, PreparationMember::LightPass };
}

}
