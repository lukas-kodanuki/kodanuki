#include "plugins/geometry/tree/world.h"

#include "plugins/detail/pilot/measure.h"
#include "plugins/geometry/tree/build.h"
#include "plugins/geometry/tree/pass.h"
#include "plugins/geometry/tree/walk.h"
#include "plugins/transport/paths/camera.h"
#include "plugins/transport/paths/strategy.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace kodanuki {

struct PilotBuild {
	const SceneFile& file;
	const Choices& choices;
	const Region& region;
	const PathsWorld& paths;
};

Tree coarseTree(const PilotBuild& build, const Detail& detail) {
	const std::vector<DetailMember> members = { DetailMember::ByDistance };
	const std::vector<PassMember> none      = {};

	const SceneDetailQuery query = {
		.scene   = build.file.scene,
		.shot    = build.file.shot,
		.region  = build.region,
		.members = members,
	};

	Detail coarse = sceneDetail(query);
	coarse.least  = detail.least;
	coarse.share  = pilotCoarseShare;

	const BuildInput input = {
		.scene    = build.file.scene,
		.detail   = coarse,
		.passes   = none,
		.region   = build.region,
		.depthCap = build.choices.depth >= 0 ? build.choices.depth : depthCapOf(build.region, detail.least),
		.workers  = build.choices.threads,
		.faces    = build.choices.faces,
	};

	return buildTree(input);
}

Pilot measuredPilot(const PilotBuild& build, const Detail& detail) {
	const Shot& shot          = build.file.shot;
	const Tree coarse         = coarseTree(build, detail);
	const Materials materials = featuredMaterials(build.paths.features, materialsOf(build.file.looks, build.paths.smoothGlass), build.paths.smoothGlass);
	const Lights lights       = lightsOf(build.file.lighting);
	const Lens lens           = lensOf(shot.camera, shot.render);
	const Sampling sampling   = coreSampling();
	const Strategies chosen   = coreStrategies();

	const Geometry geometry = treeGeometry(build.file.scene, coarse);

	const Tracing tracing = {
		.geometry   = geometry,
		.scene      = build.file.scene,
		.looks      = build.file.looks,
		.materials  = materials,
		.lights     = lights,
		.lens       = lens,
		.sampling   = sampling,
		.strategies = chosen,
		.features   = build.paths.features,
		.limits     = transportLimits(shot.render),
		.seed       = std::uint64_t(shot.render.seed),
		.film       = stillFilm(),
		.recording  = false,
	};

	const PilotFlight flight = {
		.tracing = tracing,
		.view    = detail.view,
		.threads = build.choices.threads,
	};

	const std::vector<Need> needs = pilotNeeds(flight);

	const PilotInput input = {
		.region = build.region,
		.needs  = needs,
		.view   = detail.view,
	};

	return pilotOf(input);
}

bool pilotChosen(const Choices& choices) {
	return chosenIn(choices, "detail/pilot");
}

std::vector<DetailMember> chosenDetailMembers(const Choices& choices) {
	std::vector<DetailMember> members = { DetailMember::ByDistance };
	for (const std::string& name : choices.owned) {
		if (name.rfind("detail/", 0) == 0) {
			members.push_back(*detailMemberNamed(name.substr(7)));
		}
	}
	return members;
}

std::vector<PassMember> chosenPassMembers(const Choices& choices) {
	std::vector<PassMember> members;
	for (const std::string& name : choices.owned) {
		if (name.rfind("build/", 0) == 0) {
			members.push_back(*passMemberNamed(name.substr(6)));
		}
	}
	return members;
}

BuiltGeometry builtGeometry(const SceneFile& file, const Choices& choices, const PathsWorld& paths) {
	const Region region = choices.framed ? choices.region : sceneRegion(file.scene, file.shot, choices.threads);

	const std::vector<DetailMember> members = chosenDetailMembers(choices);

	const SceneDetailQuery query = {
		.scene   = file.scene,
		.shot    = file.shot,
		.region  = region,
		.members = members,
	};

	Detail detail = sceneDetail(query);
	detail.least  = choices.least > 0 ? choices.least : detail.least;

	const PilotBuild pilot = {
		.file    = file,
		.choices = choices,
		.region  = region,
		.paths   = paths,
	};

	detail.pilot = pilotChosen(choices) ? measuredPilot(pilot, detail) : detail.pilot;
	releasedFreeMemory();
	const std::vector<PassMember> passes = chosenPassMembers(choices);

	const BuildInput input = {
		.scene    = file.scene,
		.detail   = detail,
		.passes   = passes,
		.region   = region,
		.depthCap = choices.depth >= 0 ? choices.depth : depthCapOf(region, detail.least),
		.workers  = choices.threads,
		.faces    = choices.faces,
	};

	Tree tree = buildTree(input);
	releasedFreeMemory();
	return BuiltGeometry {
		.detail = std::move(detail),
		.tree   = std::move(tree),
	};
}

}
