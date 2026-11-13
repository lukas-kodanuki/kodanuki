#include "core/preparation.h"

#include "core/plugin.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

namespace kodanuki {

Colour preparedLight(const Preparations& prepared, std::size_t pixel) {
	Colour light = { 0, 0, 0 };
	for (const Plugin& plugin : prepared.plugins) {
		if (plugin.hooks.broughtLight) {
			light = light + plugin.hooks.broughtLight(prepared.states, pixel);
		}
	}
	return light;
}

Colour preparedSpread(const Preparations& prepared, std::size_t pixel) {
	Colour spread = { 0, 0, 0 };
	for (const Plugin& plugin : prepared.plugins) {
		if (plugin.hooks.broughtSpread) {
			spread = spread + plugin.hooks.broughtSpread(prepared.states, pixel);
		}
	}
	return spread;
}

bool rereadBy(const Plugin& plugin, Change change, Reads reads, Change flag) {
	return readsOf(plugin.reads, reads) && changedBy(change, flag);
}

bool preparesAgain(const Plugin& plugin, Change change) {
	const bool scene = rereadBy(plugin, change, Reads::Lights, Change::Lights) || rereadBy(plugin, change, Reads::Looks, Change::Looks) || rereadBy(plugin, change, Reads::Shapes, Change::Shapes);
	return changedBy(change, Change::Cut) || rereadBy(plugin, change, Reads::Camera, Change::Moved) || scene;
}

bool placedIn(const std::vector<Plugin>& plugins, const std::string& name) {
	return std::any_of(plugins.begin(), plugins.end(), [&](const Plugin& plugin) {
		return plugin.name == name;
	});
}

bool readyAfter(const Plugin& plugin, const std::vector<Plugin>& waiting, const std::vector<Plugin>& placed) {
	return std::all_of(plugin.after.begin(), plugin.after.end(), [&](const std::string& name) {
		return placedIn(placed, name) || !placedIn(waiting, name);
	});
}

std::vector<Plugin> orderedPreparations(const std::vector<Plugin>& plugins) {
	std::vector<Plugin> waiting;
	std::copy_if(plugins.begin(), plugins.end(), std::back_inserter(waiting), [](const Plugin& plugin) {
		return plugin.hooks.prepareFrame != nullptr;
	});
	std::vector<Plugin> placed;
	while (placed.size() < waiting.size()) {
		const auto next = std::find_if(waiting.begin(), waiting.end(), [&](const Plugin& plugin) {
			return !placedIn(placed, plugin.name) && readyAfter(plugin, waiting, placed);
		});
		if (next == waiting.end()) {
			std::fprintf(stderr, "kodanuki: the preparations name each other in a ring\n");
			std::exit(1);
		}
		placed.push_back(*next);
	}
	return placed;
}

}
