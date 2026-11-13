#pragma once

#include "core/camera.h"
#include "core/geometry.h"
#include "core/history.h"
#include "core/state.h"
#include "core/step.h"
#include "core/stopping.h"
#include "math/colour.h"

#include <cstddef>
#include <string>
#include <vector>

namespace kodanuki {

struct Plugin;

/**
 * What a frame's preparations left, as the pixel pass and the
 * Reconstruction plugins read it: the chosen preparing plugins and the
 * kept states their hooks read.
 */
struct Preparations {
	/**
	 * The preparing plugins, in order.
	 */
	const std::vector<Plugin>& plugins;
	/**
	 * The kept states.
	 */
	States& states;
};

/**
 * Return what the preparations brought a pixel directly: the sum of
 * every preparing plugin's broughtLight, zero where none brings any.
 *
 * @param prepared The preparations.
 * @param pixel The pixel, by rows from the top left.
 */
Colour preparedLight(const Preparations& prepared, std::size_t pixel);

/**
 * Return the variance of what preparedLight() brings a pixel, per
 * channel: the sum of every preparing plugin's broughtSpread.
 *
 * @param prepared The preparations.
 * @param pixel The pixel, by rows from the top left.
 */
Colour preparedSpread(const Preparations& prepared, std::size_t pixel);

/**
 * Return whether a preparation runs again in a frame of this change:
 * every one on a Cut, and one that reads the Camera, Lights, Looks or
 * Shapes (Plugin.reads) on a Moved, Lights, Looks or Shapes change.
 *
 * @param plugin The preparing plugin.
 * @param change What changed since the frame before.
 */
bool preparesAgain(const Plugin& plugin, Change change);

/**
 * Return the plugins that fill prepareFrame, each after the plugins it
 * names in Plugin.after that are among them, else in the given order.
 *
 * @param plugins The chosen plugins.
 */
std::vector<Plugin> orderedPreparations(const std::vector<Plugin>& plugins);

} // namespace kodanuki
