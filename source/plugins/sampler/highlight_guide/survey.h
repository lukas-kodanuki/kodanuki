#pragma once

#include "plugins/sampler/highlight_guide/guide.h"
#include "plugins/transport/paths/strategy.h"

namespace kodanuki {

/**
 * What the survey and the pixels' guides are built from.
 */
struct GuideFrame {
	/**
	 * What the transport reads: the tree, the scene, the finishes, the
	 * lights, the lens, the least lift and the seed.
	 */
	const Tracing& tracing;
	/**
	 * The guide's tunings.
	 */
	GuideTunings tunings;
	/**
	 * How many threads.
	 */
	int threads;
};

/**
 * Take the survey: every eighth pixel each way, from the fourth, looks
 * for the highlights its surface sees. There is none where the scene
 * has no lamp.
 *
 * Each survey point casts its middle ray. A surface that refracts is
 * passed over. Elsewhere 128 directions are drawn through the material
 * off counter-based numbers keyed apart from any sample. Each that meets
 * a sharp surface of facet width at most 0.125 that is not glass is a
 * start: at most three to a leaf, 0.05 radians apart, sixteen in all.
 * Each start and lamp walks a chain. A settled point that lies on the
 * scene's surface (the leaf decides the field there, within 1e-4 of
 * zero, growing toward the viewer, the lamp in front of it) is a
 * highlight, unless one of the same leaf and lamp lies within 0.05.
 * Each point keeps its first 48, brightest first by the lamp's glare
 * times the spot's size, and the points on a broad surface are filed by
 * cell.
 *
 * @param frame The transport, the tunings and the threads.
 */
HighlightSurvey surveyOf(const GuideFrame& frame);

/**
 * Build every pixel's guide and pack the live ones by rows, with an
 * empty survey for the caller to move the survey into.
 *
 * Each pixel casts its middle ray and draws eight directions through
 * the material for its lobe's sharpness. Then:
 * - The highlights of the four nearest survey points on the same
 *   material are moved by their motion and weighed by nearness, by the
 *   light they bring and by the material's density at their middle.
 * - On a surface that is not broad, a spot wider than the lobe and
 *   within one lobe width of the mirror direction is narrowed by the
 *   lobe.
 * - The two heaviest are walked again from the pixel.
 * - The heaviest tunings.lumps are kept, and those under a hundredth of
 *   the heaviest are dropped.
 * - The share is the light the lumps bring against the direct light,
 *   held between the least and the most share.
 *
 * @param frame The transport, the tunings and the threads.
 * @param survey The survey.
 */
HighlightGuides pixelGuides(const GuideFrame& frame, const HighlightSurvey& survey);

}
