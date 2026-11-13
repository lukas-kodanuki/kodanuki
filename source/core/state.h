#pragma once

#include <memory>
#include <string>
#include <vector>

namespace kodanuki {

/**
 * The state a plugin keeps, which only the plugin's own accessor
 * casts.
 */
struct State {
	/**
	 * The plugin's name.
	 */
	std::string name;
	/**
	 * The state; freed with the last holder.
	 */
	std::shared_ptr<void> data;
};

/**
 * The states of the plugins at one lifetime: the run's, a frame's or a
 * worker's. A plugin holds at most one.
 */
struct States {
	/**
	 * The states, in the order they were opened.
	 */
	std::vector<State> held;
};

/**
 * Return the state a plugin keeps among states, null where it keeps
 * none.
 *
 * @param states The states.
 * @param name The plugin's name.
 */
void* stateIn(const States& states, const std::string& name);

/**
 * Return the state a plugin keeps among states, opened by the given
 * function where it keeps none yet.
 *
 * @param states The states.
 * @param name The plugin's name.
 * @param opened Returns the plugin's fresh state.
 */
void* heldState(States& states, const std::string& name, std::shared_ptr<void> (*opened)());

} // namespace kodanuki
