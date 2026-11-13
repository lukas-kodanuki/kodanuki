#include "core/reconstruction.h"

#include "core/plugin.h"

#include <utility>

namespace kodanuki {

Image reconstructedFrame(const std::vector<Plugin>& plugins, const ReconstructionQuery& query, States& states) {
	Image shown = blankImage(query.frame.record.width, query.frame.record.height);
	for (const Plugin& plugin : plugins) {
		shown = plugin.hooks.reconstructFrame(states, std::move(shown), query);
	}
	return shown;
}

bool readsAny(const std::vector<Plugin>& plugins, Reads flag) {
	for (const Plugin& plugin : plugins) {
		if (readsOf(plugin.reads, flag)) {
			return true;
		}
	}
	return false;
}

bool readsSurfaces(const std::vector<Plugin>& plugins) {
	return readsAny(plugins, Reads::Surfaces);
}

bool readsRecords(const std::vector<Plugin>& plugins) {
	return readsAny(plugins, Reads::Records);
}

}
