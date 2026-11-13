#pragma once

#include "core/preparation.h"
#include "core/shown.h"
#include "core/state.h"
#include "shared/image.h"

#include <vector>

namespace kodanuki {

struct Plugin;

/**
 * What a Reconstruction plugin reads of a frame of a sequence.
 *
 * The chosen plugins run after every pixel of the frame has been
 * traced, in the order the run lists them, each from what the ones
 * before it show. A plugin reads the frame and its own state among the
 * kept states, never changes the scene, the tree or the preparations,
 * and drops its state's history on a Cut, Lights or Scene change
 * (ShownFrame.restarted). Any plugin may run on any frame: a still is
 * one frame of many samples.
 */
struct ReconstructionQuery {
	/**
	 * The frame: its drawn and shown sizes, its lenses, its records and
	 * estimates.
	 */
	const ShownFrame& frame;
	/**
	 * What the frame's preparations left.
	 */
	const Preparations& prepared;
};

/**
 * Run the plugins' reconstructFrame hooks in order and return what the
 * last one shows, starting from a blank image at the drawn size. Each
 * keeps its state among the states; a plugin that does not run keeps
 * none.
 *
 * @param plugins The Reconstruction plugins.
 * @param query The frame.
 * @param states The kept states, which the plugins update.
 */
Image reconstructedFrame(const std::vector<Plugin>& plugins, const ReconstructionQuery& query, States& states);

/**
 * Return whether any of the plugins reads the G-buffer
 * (ShownFrame.surfaces): Reads::Surfaces.
 *
 * @param plugins The plugins.
 */
bool readsSurfaces(const std::vector<Plugin>& plugins);

/**
 * Return whether any of the plugins reads what the frame's samples
 * left (ShownFrame.record's light and first surfaces): Reads::Records.
 *
 * @param plugins The plugins.
 */
bool readsRecords(const std::vector<Plugin>& plugins);

}
