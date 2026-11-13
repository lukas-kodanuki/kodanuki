#include "core/plugin.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace kodanuki {

Hooks noHooks() {
	return Hooks {
		.openRenderer     = nullptr,
		.nextFrame        = nullptr,
		.openWorld        = nullptr,
		.buildWorld       = nullptr,
		.openFrame        = nullptr,
		.openWorker       = nullptr,
		.extendRays       = nullptr,
		.arriveAtHits     = nullptr,
		.connectLights    = nullptr,
		.scatterWalks     = nullptr,
		.startWalk        = nullptr,
		.branchWalks      = nullptr,
		.walkColour       = nullptr,
		.walkBranched     = nullptr,
		.firstSurface     = nullptr,
		.cameraSurface    = nullptr,
		.deliverFrame     = nullptr,
		.hostRenderer     = nullptr,
		.playedClip       = nullptr,
		.closeRenderer    = nullptr,
		.observed         = nullptr,
		.tallied          = nullptr,
		.published        = nullptr,
		.blocks           = nullptr,
		.prepareFrame     = nullptr,
		.broughtLight     = nullptr,
		.broughtSpread    = nullptr,
		.trialSamples     = nullptr,
		.trialSplit       = nullptr,
		.wantedSamples    = nullptr,
		.allottedSamples  = nullptr,
		.allottedSplit    = nullptr,
		.reconstructFrame = nullptr,
		.needed           = nullptr,
		.openWorkers      = nullptr,
		.hostWorkers      = nullptr,
		.ranStep          = nullptr,
		.closeWorkers     = nullptr,
		.uploadBlock      = nullptr,
	};
}

bool readsOf(Reads reads, Reads flag) {
	return (std::uint8_t(reads) & std::uint8_t(flag)) != 0;
}

Reads joinedReads(Reads a, Reads b) {
	return Reads(std::uint8_t(a) | std::uint8_t(b));
}

Reads everyRead() {
	return joinedReads(joinedReads(Reads::Camera, Reads::Lights), joinedReads(Reads::Looks, Reads::Shapes));
}

const std::vector<Plugin>& builtPlugins() {
	static const std::vector<Plugin> plugins = everyPlugin();
	return plugins;
}

bool pluginBuilt(const std::string& name) {
	const std::vector<Plugin>& plugins = builtPlugins();
	return std::any_of(plugins.begin(), plugins.end(), [&](const Plugin& plugin) {
		return plugin.name == name;
	});
}

std::uint32_t pluginIndex(const std::string& name) {
	const std::vector<Plugin>& plugins = builtPlugins();
	for (std::size_t k = 0; k < plugins.size(); ++k) {
		if (plugins[k].name == name) {
			return std::uint32_t(k);
		}
	}
	return 0xffffffff;
}

Plugin pluginNamed(const std::string& name) {
	if (!pluginBuilt(name)) {
		std::fprintf(stderr, "kodanuki: this build holds no plugin %s\n", name.c_str());
		std::exit(1);
	}
	const std::vector<Plugin>& plugins = builtPlugins();
	return *std::find_if(plugins.begin(), plugins.end(), [&](const Plugin& plugin) {
		return plugin.name == name;
	});
}

} // namespace kodanuki
