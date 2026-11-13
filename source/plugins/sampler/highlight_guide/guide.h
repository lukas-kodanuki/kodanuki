#pragma once

#include "core/state.h"
#include "math/vector.h"
#include "plugins/transport/paths/light/light.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kodanuki {

/**
 * The most lumps a guide holds.
 */
constexpr std::uint32_t mostLumps = 12;

/**
 * The mark of a pixel that has no live guide.
 */
constexpr std::uint32_t unguided = 0xFFFFFFFFu;

/**
 * The guide's tunings.
 */
struct GuideTunings {
	/**
	 * How many lumps a guide keeps, from 1 to mostLumps.
	 */
	std::uint32_t lumps;
	/**
	 * The least share of draws a pixel's guide takes.
	 */
	double leastShare;
	/**
	 * The most share of draws a pixel's guide takes, which wins over the
	 * least.
	 */
	double mostShare;
	/**
	 * The share of draws a deep guide takes.
	 */
	double deepShare;
	/**
	 * The least density of the ray a deep guide is built at the end of.
	 */
	double deepLeastDensity;
	/**
	 * Whether surfaces past the first bounce build deep guides.
	 */
	bool deep;
};

/**
 * One highlight a surface sees in a shiny surface: a Gaussian over the
 * plane one unit along an axis, around where the direction to the
 * highlight crosses it.
 *
 * A direction is read on the plane by where it crosses it.
 */
struct Lump {
	/**
	 * The unit axis the plane stands one unit along.
	 */
	math::FloatVector axis;
	/**
	 * The first unit direction across the plane.
	 */
	math::FloatVector across;
	/**
	 * The second unit direction across the plane.
	 */
	math::FloatVector up;
	/**
	 * The Gaussian's middle along across and up.
	 */
	std::array<float, 2> mean;
	/**
	 * The inverse of the spread, the upper triangle by rows.
	 */
	std::array<float, 3> precision;
	/**
	 * The lower Cholesky factor of the spread a draw is scaled by, the
	 * lower triangle by rows.
	 */
	std::array<float, 3> factor;
	/**
	 * The Gaussian's height at its middle.
	 */
	float height;
	/**
	 * The chance this lump is drawn among the guide's.
	 */
	float weight;
	/**
	 * The point on the shiny surface that shows the highlight.
	 */
	math::FloatVector point;
};

/**
 * The highlights one surface may draw toward.
 *
 * A pixel's guide is built once before any sample and never changes. A
 * deep guide is built for a surface past the first bounce.
 */
struct Guide {
	/**
	 * Whether it holds any lump.
	 */
	bool live;
	/**
	 * The material of the surface it was built for.
	 */
	std::uint32_t material;
	/**
	 * How many lumps are in use.
	 */
	std::uint32_t count;
	/**
	 * The chance a draw comes from the lumps rather than the material.
	 */
	float share;
	/**
	 * The half angle in radians a draw from the lumps is known to.
	 */
	float sharpness;
	/**
	 * The lumps, the first count of them in use.
	 */
	std::array<Lump, mostLumps> lumps;
};

/**
 * A lump as the frame keeps it, in single precision.
 *
 * The second direction across, the precision and the height are worked
 * out again from these when it is read, so a draw and its density agree
 * to the bit.
 */
struct PackedLump {
	/**
	 * The axis.
	 */
	std::array<float, 3> axis;
	/**
	 * The first direction across.
	 */
	std::array<float, 3> across;
	/**
	 * The point that shows the highlight.
	 */
	std::array<float, 3> point;
	/**
	 * The middle.
	 */
	std::array<float, 2> mean;
	/**
	 * The lower factor of the spread.
	 */
	std::array<float, 3> factor;
	/**
	 * The weight, before the guide's weights are made to sum to one.
	 */
	float weight;
};

/**
 * A live guide's head as the frame keeps it.
 */
struct PackedGuide {
	/**
	 * Where its lumps start among its row's lumps.
	 */
	std::uint32_t first;
	/**
	 * How many lumps, zero for a guide that is not live.
	 */
	std::uint32_t count;
	/**
	 * The material it was built for.
	 */
	std::uint32_t material;
	/**
	 * The share of draws.
	 */
	float share;
	/**
	 * The half angle a draw from the lumps is known to.
	 */
	float sharpness;
};

/**
 * The live guides of one row of pixels.
 */
struct GuideRow {
	/**
	 * The heads, in the order the row's pixels were packed.
	 */
	std::vector<PackedGuide> heads;
	/**
	 * Their lumps.
	 */
	std::vector<PackedLump> lumps;
};

/**
 * One place on a shiny surface where it mirrors a lamp, as one survey
 * point saw it.
 */
struct Highlight {
	/**
	 * The shiny surface's material.
	 */
	std::uint32_t material;
	/**
	 * The scene leaf the point is on.
	 */
	std::uint32_t leaf;
	/**
	 * The lamp it shows.
	 */
	std::uint32_t light;
	/**
	 * The point.
	 */
	math::Vector point;
	/**
	 * The shiny surface's normal where the walk to the point started.
	 */
	math::Vector normal;
	/**
	 * The shiny surface's facet width, the GGX alpha of its sharpest
	 * lobe.
	 */
	double width;
	/**
	 * The first direction the spread is measured along.
	 */
	math::Vector across;
	/**
	 * The second.
	 */
	math::Vector up;
	/**
	 * The spread over the plane one unit away, the upper triangle by
	 * rows.
	 */
	std::array<double, 3> spread;
	/**
	 * How far the survey point stood from the point.
	 */
	double distance;
	/**
	 * Where the survey point stood.
	 */
	math::Vector origin;
	/**
	 * The two directions across the survey point's surface the motion is
	 * measured along.
	 */
	std::array<math::Vector, 2> sideways;
	/**
	 * How far the point moves per unit the viewer moves along each.
	 */
	std::array<math::Vector, 2> motion;
	/**
	 * The material of the survey point's surface.
	 */
	std::uint32_t seen;
	/**
	 * Whether that surface draws its wide lobe at least a quarter of the
	 * time.
	 */
	bool broad;
	/**
	 * That surface's normal, facing the eye.
	 */
	math::Vector seenNormal;
};

/**
 * The highlights found once a frame at every eighth pixel each way.
 */
struct HighlightSurvey {
	/**
	 * How many survey points across, zero where there was no survey.
	 */
	int across;
	/**
	 * How many down.
	 */
	int down;
	/**
	 * Each survey point's highlights by rows, the brightest first, at
	 * most 48.
	 */
	std::vector<std::vector<Highlight>> spots;
	/**
	 * The survey points on a broad surface, filed by the 0.3-unit cell
	 * they stand in and the 26 around it, by the cell's key.
	 */
	std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> filed;
};

/**
 * HighlightGuide's frame state: the survey and every pixel's guide.
 */
struct HighlightGuides {
	/**
	 * The survey, which deep guides read.
	 */
	HighlightSurvey survey;
	/**
	 * How many pixels a row holds.
	 */
	std::uint32_t width;
	/**
	 * Which head of its row each pixel has, by rows, or unguided.
	 */
	std::vector<std::uint32_t> heads;
	/**
	 * The rows.
	 */
	std::vector<GuideRow> rows;
	/**
	 * The tunings.
	 */
	GuideTunings tunings;
};

/**
 * A guide built for one surface, packed as the frame keeps one.
 */
struct DeepGuide {
	/**
	 * The head, its first lump at 0.
	 */
	PackedGuide head;
	/**
	 * The lumps, the first head.count of them in use.
	 */
	std::array<PackedLump, mostLumps> lumps;
};

/**
 * A guide's lumps turned to face where a path stands.
 */
struct SeenLumps {
	/**
	 * The guide, not live where none steers the place.
	 */
	Guide guide;
	/**
	 * Each lump's middle as seen from the place.
	 */
	std::array<std::array<float, 2>, mostLumps> centres;
};

/**
 * The guide a path keeps.
 */
struct GuideState {
	/**
	 * The pixel whose guide the first surface draws from, by rows, or
	 * unguided.
	 */
	std::uint32_t pixel;
	/**
	 * The lumps that steer draws at the vertex the path stands on,
	 * unpacked and turned to face it once when the path reaches it: the
	 * pixel's guide on the first surface, the deep guide built for the
	 * surface past it.
	 */
	SeenLumps steering;
	/**
	 * The pixel whose guide's lumps steering holds unpacked, or
	 * unguided where it holds a deep guide's or none. A walk that draws
	 * the same pixel again turns them without unpacking them again.
	 */
	std::uint32_t unpacked;
};

/**
 * Where a path stands that may be steered.
 */
struct GuidePlace {
	/**
	 * The material of the surface it stands on.
	 */
	std::uint32_t material;
	/**
	 * Whether it travels inside a solid.
	 */
	bool inside;
	/**
	 * The point it stands on.
	 */
	math::Vector point;
};

/**
 * A pair of directions whose lump density is asked.
 */
struct LumpPair {
	/**
	 * The unit surface normal, facing the outgoing side.
	 */
	math::Vector normal;
	/**
	 * The unit direction the light leaves toward.
	 */
	math::Vector outgoing;
	/**
	 * The unit direction asked.
	 */
	math::Vector incoming;
};

/**
 * A direction asked of the lumps.
 */
struct LumpQuery {
	/**
	 * The unit surface normal, facing the outgoing side.
	 */
	math::Vector normal;
	/**
	 * The unit direction the light leaves toward.
	 */
	math::Vector outgoing;
	/**
	 * The number that picks the lump by its weight.
	 */
	double pick;
	/**
	 * The two numbers of the point across the lump.
	 */
	math::UnitPair pair;
};

/**
 * A direction the lumps drew.
 */
struct LumpDraw {
	/**
	 * Whether one was drawn above the surface.
	 */
	bool found;
	/**
	 * The unit direction.
	 */
	math::Vector incoming;
};

/**
 * Where a deep guide is built.
 */
struct DeepPlace {
	/**
	 * The surface's material.
	 */
	std::uint32_t material;
	/**
	 * The point the path stands on.
	 */
	math::Vector point;
	/**
	 * The unit surface normal there, facing the path.
	 */
	math::Vector normal;
};

/**
 * The frame's lights and guides a deep guide is built from.
 */
struct DeepGuides {
	/**
	 * The frame's guides and their survey.
	 */
	const HighlightGuides& guides;
	/**
	 * The lights the survey was taken in.
	 */
	const Lights& lights;
};

/**
 * Steer a path on its first surface by its pixel's guide: the guide's
 * lumps unpacked into the state and turned to face the place, where the
 * pixel has a guide built for the place's material and the path is in
 * the open; no lumps elsewhere.
 *
 * @param guides The frame's guides.
 * @param place The material, whether the path is inside and the point.
 * @param state The path's guide, its pixel read and its lumps written in place (rule 46).
 */
void steerByPixel(const HighlightGuides& guides, const GuidePlace& place, GuideState& state);

/**
 * Steer a path past its first surface by the deep guide built for it,
 * as steerByPixel() steers by the pixel's.
 *
 * @param deep The deep guide, not live where none was built.
 * @param place The material, whether the path is inside and the point.
 * @param state The path's guide, its lumps written in place (rule 46).
 */
void steerByDeep(const DeepGuide& deep, const GuidePlace& place, GuideState& state);

/**
 * Leave a path's guide with no lumps, so nothing steers its draws.
 *
 * @param state The path's guide, its lumps emptied in place (rule 46).
 */
void unsteered(GuideState& state);

/**
 * Turn a live guide's lumps to face a place: each lump's middle moves to
 * where the direction to its point crosses the lump's plane.
 *
 * @param guide A live guide.
 * @param place Where the path stands.
 */
SeenLumps seenLumps(const Guide& guide, const math::Vector& place);

/**
 * Return the density of drawing a direction from the lumps: zero below
 * the surface, and for every direction when the outgoing one is below
 * it, where a draw from the lumps is refused.
 *
 * Each lump's Gaussian is cut off where its exponent passes 60 and read
 * over the solid angle.
 *
 * @param lumps The seen lumps.
 * @param pair The normal, the outgoing direction and the direction asked.
 */
double lumpDensity(const SeenLumps& lumps, const LumpPair& pair);

/**
 * Draw a direction from the lumps: the lump by its weight, then a point
 * of its cut-off Gaussian through its factor. A draw below the surface,
 * or from below it, finds nothing.
 *
 * @param lumps The seen lumps.
 * @param query The normal, the outgoing direction, the lump number and the two numbers.
 */
LumpDraw lumpDirection(const SeenLumps& lumps, const LumpQuery& query);

/**
 * Build the deep guide of a surface, or one that is not live.
 *
 * It reads the broad survey points filed in the surface's 0.3-unit
 * cell, at most four of them on the same material, standing within 1.5
 * cells and facing within 0.9 of the same way. Each brings its three
 * brightest highlights, moved by their motion to where the surface
 * stands and weighed by nearness, the lamp's glare, the spot's size and
 * how squarely the surface faces it. The heaviest tunings.lumps of them
 * are kept and take tunings.deepShare of the draws.
 *
 * @param deep The frame's guides and the lights.
 * @param place The material, the point and the normal facing the path.
 */
DeepGuide deepGuide(const DeepGuides& deep, const DeepPlace& place);

/**
 * Return a guide that is not live.
 */
Guide noGuide();

/**
 * Return a guide that is not live, packed.
 */
DeepGuide noDeepGuide();

/**
 * Return a guide read back in double from its packed head and lumps,
 * its weights made to sum to one; not live where the head holds no
 * lump.
 *
 * @param head The head.
 * @param lumps The lumps, from the head's first.
 */
Guide unpackedGuide(const PackedGuide& head, const PackedLump* lumps);

/**
 * Return a guide's head and lumps in single precision. A guide that is
 * not live has a head of no lumps.
 *
 * @param guide The guide.
 * @param first Where its lumps will start.
 * @param lumps Where its lumps are added (rule 46).
 */
PackedGuide packedGuide(const Guide& guide, std::uint32_t first, std::vector<PackedLump>& lumps);

/**
 * Return a lamp's glare: the luminance of its power over its face, or
 * ten thousand times its luminance for a point or a directional light.
 *
 * @param lamp The lamp.
 */
double lampGlare(const Lamp& lamp);

/**
 * Return the key a point is filed under among the cells of a side,
 * shifted by whole cells along each axis.
 *
 * @param point The point.
 * @param side The cell's side.
 * @param shift The whole cells along x, y and z.
 */
std::uint64_t filedKey(const math::Vector& point, double side, const std::array<int, 3>& shift);

/**
 * Return the default tunings: 12 lumps, shares of a pixel's guide
 * between 0.02 and 0.5, deep guides on with a share of 0.3 past rays of
 * density 3 and more.
 */
GuideTunings defaultGuideTunings();

/**
 * Return guides with no survey and no live guide, for a frame where
 * HighlightGuide is off.
 */
HighlightGuides noHighlightGuides();

/**
 * Return the survey and pixel guides the plugin keeps among the kept states, opened empty
 * where no preparation filled it yet: the plugin's one accessor.
 *
 * @param states The kept states.
 */
HighlightGuides& highlightGuidesState(States& states);

}
