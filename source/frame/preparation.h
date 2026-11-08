#pragma once

#include "extras/strategy_correct_join/pass.h"
#include "frame/execution.h"
#include "frame/stopping.h"
#include "spectral/colour.h"
#include "transport/strategy.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * Work run once per frame before any tile, whose result paths read.
 */
enum class PreparationMember : std::uint8_t {
	/**
	 * Charts the sky, or leaves it uncharted where the lamps outshine it
	 * (SkyMap's state).
	 */
	SkyChart,
	/**
	 * Gathers the glow cells as a grid over the region at depth 6
	 * (GlowingSurfaces' state).
	 */
	GlowGrid,
	/**
	 * Bounds what may relay a light path (TowardMirrors' state).
	 */
	MirrorBalls,
	/**
	 * Takes the survey of highlights and builds every pixel's guide
	 * (HighlightGuide's state).
	 */
	GuideSurvey,
	/**
	 * Sets the lamp shares and the join share, which Join and Connect
	 * read, and traces the light pass where join traces (Join's state).
	 * It runs last: it reads the mirror balls and each pixel's guide.
	 */
	LightPass,
};

/**
 * What a preparation's result depends on, so that frames which follow
 * one another rerun only what a change touched.
 */
enum class PreparationReads : std::uint8_t {
	/**
	 * The scene and its tree only: the result holds while the camera
	 * moves.
	 */
	Load,
	/**
	 * The lens as well: the result is worked out again whenever the
	 * camera moves.
	 */
	Camera,
};

/**
 * What a frame hands the transport: the Sampler and Strategy members
 * with the states the preparations filled.
 */
struct Prepared {
	/**
	 * The Sampler members and their states.
	 */
	Sampling sampling;
	/**
	 * The Strategy members and their states.
	 */
	Strategies strategies;
	/**
	 * What the light pass brought, nothing where it traced none.
	 */
	CarriedLight light;
};

/**
 * What a preparation reads.
 */
struct PreparationQuery {
	/**
	 * What the transport reads with the preparations so far.
	 */
	const Tracing& tracing;
	/**
	 * The stopping, which the light pass's rounds read.
	 */
	const Stopping& stopping;
	/**
	 * The member that runs the light pass's paths.
	 */
	ExecutionMember execution;
	/**
	 * How many threads it may use.
	 */
	int threads;
};

/**
 * Run one member and store what it prepared in the preparations: the
 * point's dispatch.
 *
 * The members run in the order the frame lists them, and the query's
 * tracing holds the states the earlier ones filled; SkyChart, GlowGrid,
 * MirrorBalls and GuideSurvey read none of them, and LightPass reads the
 * mirror balls and the guides. The member works out its state before it
 * replaces the old one, so the query's tracing may read the preparations
 * it fills. They are filled in place because the guides and the light
 * pass grow with the pixels, and a copy would hold them twice.
 *
 * @param member The member.
 * @param query The transport, the stopping, the Execution member and
 *        the threads.
 * @param prepared The preparations so far, which the member fills.
 */
void preparedBy(PreparationMember member, const PreparationQuery& query, Prepared& prepared);

/**
 * Return what the preparations brought a pixel directly: the light
 * pass's carriedMean(), zero where it traced none.
 *
 * @param prepared The preparations.
 * @param pixel The pixel, by rows from the top left.
 */
Colour preparedLight(const Prepared& prepared, std::size_t pixel);

/**
 * Return the variance of what preparedLight() brings a pixel, per
 * channel: the light pass's carriedSpread().
 *
 * @param prepared The preparations.
 * @param pixel The pixel, by rows from the top left.
 */
Colour preparedSpread(const Prepared& prepared, std::size_t pixel);

/**
 * Return the core's members of every point a frame prepares:
 * coreSampling() and coreStrategies().
 */
Prepared corePrepared();

/**
 * Return the members a frame draws with by default: defaultSampling(),
 * and coreStrategies() with Join on.
 */
Prepared defaultPrepared();

/**
 * Return what a preparation's result depends on: Load for SkyChart,
 * GlowGrid and MirrorBalls, Camera for GuideSurvey and LightPass.
 *
 * @param member The preparation.
 */
PreparationReads preparationReads(PreparationMember member);

/**
 * Return the preparations a frame runs by default: SkyChart, GlowGrid,
 * MirrorBalls, GuideSurvey and LightPass.
 */
std::vector<PreparationMember> defaultPreparations();

}
