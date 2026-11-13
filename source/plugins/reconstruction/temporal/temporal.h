#pragma once

#include "core/shown.h"
#include "core/state.h"
#include "math/colour.h"
#include "shared/image.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * What the temporal member keeps of one pixel of its grid
 * (TemporalGrid).
 */
struct TemporalPixel {
	/**
	 * The pixel's light, the running blend of its samples; where the
	 * state is demodulated its lighting: the blend of its samples' light
	 * divided by their albedo (lightingDivisor), which the pixel's albedo
	 * multiplies back (shownLight).
	 */
	Colour colour;
	/**
	 * The running blend of the luminance squared of what colour blends,
	 * for the spread the clamp and a later denoiser read.
	 */
	double moment;
	/**
	 * How many samples the blend holds, at most the cap; zero where the
	 * pixel holds nothing.
	 */
	float length;
	/**
	 * The distance from the lens to what the pixel shows; infinite where
	 * it shows the sky, and where it holds nothing.
	 */
	float distance;
	/**
	 * The unit shading normal there; zero on the sky.
	 */
	math::Vector normal;
	/**
	 * The scene leaf there, noLeaf on the sky.
	 */
	std::uint32_t leaf;
	/**
	 * The albedo there, which a denoiser divides the light by before it
	 * filters and multiplies it by after; zero on the sky.
	 *
	 * Where the state is demodulated it is the G-buffer's albedo of the
	 * pixel, exact for this frame. Elsewhere it is blended like the
	 * light, with the same shares, so that both average the same samples
	 * and dividing one by the other keeps the pixel's edges: a pixel with
	 * no history takes its sample's (or the nearest drawn sample's), and
	 * each sample aimed into the pixel later is blended in; elsewhere it
	 * is that of the history the pixel took.
	 */
	Colour albedo;
};

/**
 * The pixels the temporal member works on for a frame.
 */
struct TemporalGrid {
	/**
	 * The grid's width: the drawn width where the frame holds a G-buffer,
	 * else the shown width.
	 */
	int width;
	/**
	 * The grid's height.
	 */
	int height;
	/**
	 * Whether the frame holds a G-buffer (ShownFrame.surfaces), at the
	 * drawn size: then each pixel of the grid is a drawn pixel, looked at
	 * through the centre of the shown pixel its phase aims at.
	 */
	bool demodulated;
};

/**
 * The temporal member's history: every pixel of the last frame's grid.
 */
struct TemporalState {
	/**
	 * The grid's width; zero before the first frame.
	 */
	int width;
	/**
	 * The grid's height.
	 */
	int height;
	/**
	 * Whether the pixels hold lighting: the frame had a G-buffer, so each
	 * pixel's colour is its light divided by the albedo, and its surface
	 * and albedo are the G-buffer's; the pixels are then the drawn ones.
	 */
	bool demodulated;
	/**
	 * The last frame's film plan: where the history is demodulated, each
	 * pixel's camera ray passes through its filmCentre() (temporalCentre()).
	 */
	FilmPlan film;
	/**
	 * The pixels, by rows from the top left.
	 */
	std::vector<TemporalPixel> pixels;
	/**
	 * The buffer of the frame before's pixels, which the next frame
	 * writes its pixels into instead of allocating them anew; its
	 * contents mean nothing.
	 */
	std::vector<TemporalPixel> spare;
};

/**
 * The shown image of a frame and the history after it.
 */
struct Temporal {
	/**
	 * The shown image, linear, at the grid's size (temporalGrid()).
	 */
	Image image;
	/**
	 * The history the next frame reads.
	 */
	TemporalState state;
};

/**
 * Return the history of no frame.
 */
TemporalState emptyTemporal();

/**
 * Return the grid the temporal member works on for a frame: the drawn
 * pixels where the frame holds a G-buffer at the drawn size, else the
 * shown pixels.
 *
 * @param frame The frame.
 */
TemporalGrid temporalGrid(const ShownFrame& frame);

/**
 * Return where in its grid pixel a pixel of a history is looked at
 * through: the filmCentre() of its film plan where it is demodulated,
 * else the pixel's middle.
 *
 * @param state The history.
 */
FilmCentre temporalCentre(const TemporalState& state);

/**
 * Return what a light is divided by, channel by channel, to give the
 * lighting on a surface of an albedo: the albedo, or 1 where a channel's
 * albedo is below 1e-3, as on the sky, so that light there is kept as
 * it is and not multiplied back.
 *
 * @param albedo The albedo.
 */
Colour lightingDivisor(const Colour& albedo);

/**
 * Return the light a pixel of the history shows: its colour, or where
 * the history is demodulated its lighting times its albedo's
 * lightingDivisor.
 *
 * @param pixel The pixel.
 * @param demodulated Whether the history holds lighting
 *        (TemporalState.demodulated).
 */
Colour shownLight(const TemporalPixel& pixel, bool demodulated);

/**
 * Return the frame's shown image and history, from this frame's samples
 * and the history reprojected into this frame's camera: a temporal
 * upscaler.
 *
 * Each shown pixel finds the surface it shows from the drawn samples
 * around it, moves it into the frame before's camera and takes that
 * camera's history there where it shows the same surface (close in
 * distance and normal, the same leaf); elsewhere its history is dropped.
 * A shown pixel a drawn sample aimed into this frame blends that sample
 * into its history, the history clamped to the spread of the drawn
 * samples around it. Pixels with no history show the drawn samples
 * around them. The history is dropped on a restart and where the shown
 * size changes. The blend is biased: it trades the mean for less noise.
 *
 * The surface a shown pixel shows is the nearest drawn sample's, or,
 * where that finds no history, the closest of the 3x3 drawn samples
 * around it, so a thin near edge keeps its history. Its point is the
 * sample's depth along the pixel's own ray through the pinhole eye; the
 * frame before's history is read bilinearly there from the pixels that
 * show the same surface: the same leaf, normals within a cosine of 0.9,
 * distances within 3% plus the 3x3's spread of distances (at most a
 * quarter of the distance); the sky matches the sky alone. On a camera
 * that did not move the history is the pixel's own. A sample blends in
 * with a share of one over its history's length plus one, at least one
 * over the cap: 32 samples while the camera moves, 256 while it holds
 * still, so a still camera keeps converging to the mean of its samples.
 * The history is clamped, channel by channel, to the 3x3's mean plus or
 * minus 1.25 deviations while the camera moves and 6 while it holds a
 * history younger than 8 samples; an older still history is not
 * clamped. A pixel without history takes the sample aimed into it, or
 * shows the bilinear blend of the four drawn samples around it with a
 * length of zero.
 *
 * Where the frame holds a G-buffer (ShownFrame.surfaces) the history is
 * demodulated and works on the drawn pixels, the image at the drawn
 * size: every pixel is aimed each frame, and each pixel's camera ray is
 * the jittered one through the centre of the shown pixel its phase aims
 * at (filmCentre()), the frame before's pixels looked at through that
 * frame's phase (TemporalState.film). A phase that changes is not a
 * move: on a camera that did not move each pixel keeps its own history.
 * The history blends lighting, each sample's light divided by its
 * albedo's lightingDivisor, and the shown image is the lighting times the
 * G-buffer's albedo. Each pixel's surface and albedo are then the
 * G-buffer's: its point is the G-buffer's distance along the pixel's ray,
 * and the frame before's pixels match it where they show the same leaf
 * with normals within a cosine of 0.9 and their points within 3% of the
 * distance of the pixel's tangent plane (the sky the sky alone). A
 * pixel's sample counts only where it met the G-buffer's leaf (the sky
 * the sky), so a silhouette does not mix the lighting of two surfaces.
 * The clamp reads the lighting of the 3x3 drawn samples. A pixel without
 * history whose sample does not count takes the mean of the 3x3 drawn
 * samples around it on its surface (the same test as the history's),
 * else its own sample, so lighting does not bleed across edges. A
 * history of the other kind is dropped.
 *
 * @param before The history after the frame before, handed over so
 *        that its spare buffer holds this frame's pixels.
 * @param frame The frame.
 */
Temporal temporalFrame(TemporalState before, const ShownFrame& frame);

/**
 * Return the history the plugin keeps among the kept states, opened
 * empty where it keeps none yet: the plugin's one accessor.
 *
 * @param states The kept states.
 */
TemporalState& temporalState(States& states);

}
