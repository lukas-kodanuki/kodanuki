#pragma once

#include "transport/sampler.h"

namespace kodanuki {

/**
 * The two numbers a direction mixture draws with past the material's
 * own.
 */
struct MixtureNumbers {
	/**
	 * The number that picks the part: the lumps below the share, the
	 * material above it.
	 */
	double part;
	/**
	 * The number that picks the lump by its weight.
	 */
	double lump;
};

/**
 * Draw a direction from the mixture of the guide's lumps (share s) and
 * the material (1 - s): HighlightGuide's draw.
 *
 * The lumps are the ones the guide state keeps for the vertex. The
 * material's draw keeps its value and takes the mixture's density; an
 * exact material draw is weighed by 1 / (1 - s). A draw from the lumps
 * reads the direction numbers across its lump, and is weighed by the
 * material's value times the cosine over the mixture's density. Where
 * no guide steers the surface the material's own draw comes back.
 *
 * @param query The path, the guide it keeps, the materials and the numbers.
 * @param numbers The part and lump numbers.
 */
Scatter mixtureDirection(const DirectionQuery& query, const MixtureNumbers& numbers);

/**
 * Return the density mixtureDirection() draws a direction with:
 * s * lumpDensity + (1 - s) * bsdfDensity, or bsdfDensity where no guide
 * steers the surface.
 *
 * @param query The path, the guide it keeps, the direction and its own density.
 */
double mixtureDensity(const DirectionDensityQuery& query);

/**
 * Steer a path on its first surface by its pixel's guide: the lumps are
 * unpacked into its guide state and turned to face the vertex once, so
 * every draw and density there reads them as they are.
 *
 * @param guides The frame's guides.
 * @param path The path, standing on its first vertex.
 * @param state The path's guide, its lumps written in place (rule 46).
 */
void steerFirst(const HighlightGuides& guides, const Path& path, GuideState& state);

/**
 * Steer a path past its first surface by the deep guide built for the
 * vertex, as steerFirst() steers by the pixel's.
 *
 * @param deep The deep guide, not live where none was built.
 * @param path The path, standing on the vertex.
 * @param state The path's guide, its lumps written in place (rule 46).
 */
void steerDeep(const DeepGuide& deep, const Path& path, GuideState& state);

}
