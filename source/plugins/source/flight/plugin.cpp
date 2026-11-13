#include "plugins/source/flight/plugin.h"

#include "core/render.h"
#include "plugins/sink/window/exchange.h"
#include "sdf/compare.h"
#include "syntax/evaluator.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace kodanuki {

using namespace kodanuki::viewer;

struct FlightState {
	bool fresh;
	double time;
};

FlightState& flightState(const Renderer& renderer) {
	return *static_cast<FlightState*>(stateOf(renderer, "source/flight"));
}

void flightOpened(Renderer& renderer) {
	if (!stateOf(renderer, "sink/window")) {
		std::fprintf(stderr, "source/flight: needs sink/window\n");
		std::exit(1);
	}
	keptState(renderer, "source/flight", std::make_shared<FlightState>(true, 0.0));
}

bool restingAfter(const Renderer& renderer) {
	const int samples = renderer.file.shot.render.samples;
	return renderer.drawn.rounds == 0 || (samples > 0 && renderer.step.still + 1 >= std::uint32_t(samples));
}

void reloaded(Renderer& renderer, Exchange& exchange) {
	LoadedScene loaded = loadScene(renderer.options.arguments.values[1], renderer.frame.time);
	if (!loaded.error.empty()) {
		exchange.failed = loaded.error;
		return;
	}
	SceneFile next = reloadedFile(std::move(loaded.file), renderer.options);
	if (!sameSettings(renderer.file.shot, next.shot)) {
		renderer.frame.change = joinedChange(renderer.frame.change, Change::Cut);
	}
	sceneTaken(renderer, std::move(next));
	exchange.failed = {};
}

bool flightNext(Renderer& renderer) {
	FlightState& state = flightState(renderer);
	Exchange& exchange = windowExchange(renderer);
	const bool stepped = renderer.file.timed && renderer.frame.time != state.time;
	const Order order  = takenOrder(exchange, !state.fresh && !stepped && restingAfter(renderer));
	state.fresh        = false;
	state.time         = renderer.frame.time;
	if (order.quit) {
		return false;
	}
	if (order.reload) {
		reloaded(renderer, exchange);
	}
	renderer.frame.camera = order.flown ? order.camera : renderer.file.shot.camera;
	return true;
}

std::vector<std::string> flightNeeded() {
	return { "sink/window" };
}

Plugin sourceFlightPlugin() {
	Hooks hooks        = noHooks();
	hooks.openRenderer = flightOpened;
	hooks.nextFrame    = flightNext;
	hooks.needed       = flightNeeded;
	return Plugin {
		.name   = "source/flight",
		.point  = Point::Source,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
