#include "core/state.h"

namespace kodanuki {

void* stateIn(const States& states, const std::string& name) {
	for (const State& state : states.held) {
		if (state.name == name) {
			return state.data.get();
		}
	}
	return nullptr;
}

void* heldState(States& states, const std::string& name, std::shared_ptr<void> (*opened)()) {
	void* held = stateIn(states, name);
	if (held) {
		return held;
	}
	states.held.push_back(State { .name = name, .data = opened() });
	return states.held.back().data.get();
}

} // namespace kodanuki
