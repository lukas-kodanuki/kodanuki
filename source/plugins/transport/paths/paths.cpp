#include "plugins/transport/paths/paths.h"

#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace kodanuki {

static_assert(std::is_trivially_copyable_v<Walk>, "a walk is plain data");
static_assert(std::is_trivially_copyable_v<LightWalk>, "a light walk is plain data");
static_assert(std::is_trivially_copyable_v<PathsWorker>, "a worker's walks are plain data");

const PathsWorld& pathsWorld(const void* world) {
	return *static_cast<const PathsWorld*>(world);
}

const Tracing& tracingOf(const void* transported) {
	return *static_cast<const PathsFrame*>(transported)->tracing;
}

PathsWorker* pathsWorker(const Worker& worker) {
	return static_cast<PathsWorker*>(worker.walks.get());
}

Walk* walksOf(const Worker& worker) {
	return pathsWorker(worker)->walks;
}

LightWalk* leadsOf(const Worker& worker) {
	return pathsWorker(worker)->leads;
}

SmoothGlass smoothGlassNamed(const std::string& name) {
	return name == "lobe" ? SmoothGlass::Lobe : SmoothGlass::Exact;
}

std::vector<EmissionShare> chosenEmission(const Choices& choices) {
	const std::vector<EmissionShare> alone = defaultSampling().emission;

	const EmissionShare cosine = {
		.member = SamplerMember::Cosine,
		.share  = 0.5,
	};

	const EmissionShare mirrors = {
		.member = SamplerMember::TowardMirrors,
		.share  = 0.5,
	};

	const std::vector<EmissionShare> mixed = { cosine, mirrors };
	return chosenIn(choices, "strategy/connect") && chosenIn(choices, "sampler/toward_mirrors") ? mixed : alone;
}

std::vector<SamplerMember> chosenAims(const Choices& choices) {
	std::vector<SamplerMember> aims = { SamplerMember::Lamps };
	if (chosenIn(choices, "sampler/sky_map")) {
		aims.push_back(SamplerMember::SkyMap);
	}
	if (chosenIn(choices, "sampler/glowing_surfaces")) {
		aims.push_back(SamplerMember::GlowingSurfaces);
	}
	return aims;
}

Sampling chosenSampling(const Choices& choices) {
	Sampling sampling  = defaultSampling();
	sampling.sequence  = chosenIn(choices, "sampler/sobol") ? SamplerMember::Sobol : SamplerMember::Independent;
	sampling.direction = chosenIn(choices, "sampler/highlight_guide") ? SamplerMember::HighlightGuide : SamplerMember::Material;
	sampling.selection = chosenIn(choices, "sampler/each_lamp_first") ? SamplerMember::EachLampFirst : SamplerMember::ByContribution;
	sampling.aims      = chosenAims(choices);
	sampling.emission  = chosenEmission(choices);
	return sampling;
}

Strategies chosenStrategies(const Choices& choices) {
	Strategies strategies = coreStrategies();
	if (chosenIn(choices, "strategy/join")) {
		strategies.members.push_back(StrategyMember::Join);
	}
	if (chosenIn(choices, "strategy/connect")) {
		strategies.members.push_back(StrategyMember::Connect);
	}
	return strategies;
}

PathsWorld pathsWorldOf(const SceneFile& file, const Choices& choices) {
	return PathsWorld {
		.features    = featuresOf(featureQueryOf(file, choices.threads)),
		.smoothGlass = smoothGlassNamed(choices.smoothGlass),
		.sampling    = chosenSampling(choices),
		.strategies  = chosenStrategies(choices),
	};
}

void waitingSlot(const Tracing& tracing, std::uint32_t slot, Worker& worker) {
	const Walk& walk         = walksOf(worker)[slot];
	worker.pool.phases[slot] = walk.phase;
	if (!waitsOnRay(walk.phase)) {
		return;
	}
	const Wanted wanted     = wantedRay(tracing, walk, leadsOf(worker));
	worker.pool.kinds[slot] = wanted.kind;
	worker.pool.rays[slot]  = wanted.ray;
}

} // namespace kodanuki
