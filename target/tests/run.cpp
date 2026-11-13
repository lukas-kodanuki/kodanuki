#include "check.h"
#include "core/options.h"
#include "core/plugin.h"
#include "plugins/observer/progress/plugin.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace kodanuki {

void checkProgressLine() {
	const Snapshot snapshot = {
		.stage    = Stage::Frame,
		.finished = 30,
		.total    = 120,
		.frame    = 0,
		.frames   = 0,
		.staged   = 12,
		.drawing  = 10,
		.left     = 30,
	};

	check(progressLine(snapshot) == "render [######..................] 30/120 tiles  25% 10.0 s, about 30.0 s left", "the progress line shows the tiles, the share, the time so far and the time left");
}

bool pluginOfKey(const UseKey& key, const std::string& member) {
	const std::string name = key.name + "/" + member;
	const bool built       = pluginBuilt(name) && pluginNamed(name).name == name;
	return switchedByUse(name) && (built || std::filesystem::is_directory("source/plugins/" + name));
}

void checkUsePlugins() {
	for (const UseKey& key : useKeys()) {
		for (const std::string& member : key.members) {
			const std::string what = "--use " + key.name + "=" + member + " names a plugin of that point";
			check(key.name == "faces" || key.name == "smooth_glass" || pluginOfKey(key, member), what.c_str());
		}
	}
	const Choices sobol = withUse(defaultChoices(), "sampler=sobol");
	check(chosenIn(sobol, "sampler/sobol") && !chosenIn(sobol, "sampler/highlight_guide") && chosenIn(sobol, "strategy/join"), "--use sampler=sobol leaves the other samplers off and the other points as they were");
	check(chosenIn(withUse(defaultChoices(), "execution=single"), "execution/single"), "--use execution=single chooses execution/single");
	check(!switchedByUse("build/faced_hits") && !switchedByUse("feature/clouds"), "a plugin built in or out is not switched by --use");
}

void checkPlugins() {
	std::set<std::string> names;
	for (const Plugin& plugin : everyPlugin()) {
		names.insert(plugin.name);
	}
	check(names.size() == everyPlugin().size(), "every plugin has a name of its own");
	check(pluginNamed("execution/packets").point == Point::Execution, "execution/packets fills the point Execution");
	check(pluginNamed("strategy/join").effect == Effect::Exact, "strategy/join keeps the mean");
}

void checkRun() {
	checkProgressLine();
	checkUsePlugins();
	checkPlugins();
}

}
