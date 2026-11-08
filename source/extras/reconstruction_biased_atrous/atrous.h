#pragma once

#include "extras/reconstruction_biased_temporal/temporal.h"
#include "frame/shown.h"
#include "image/image.h"

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace kodanuki {

/**
 * How many pixels of a row the filter works through at once: the width
 * of its accumulators, which live on the stack so the passes vectorise.
 */
constexpr int denoiseChunk = 256;

/**
 * What the denoiser knows of the surface a shown pixel shows, for the
 * edge stops: its point, its normal and how far its neighbours may lie
 * from its plane.
 */
struct DenoiseGuide {
	/**
	 * The point's x, from the eye: along the pixel's own ray through the
	 * pinhole eye, at the pixel's distance; zero on the sky.
	 */
	float x;
	/**
	 * The point's y, from the eye.
	 */
	float y;
	/**
	 * The point's z, from the eye.
	 */
	float z;
	/**
	 * The unit normal's x; zero on the sky.
	 */
	float normalX;
	/**
	 * The unit normal's y.
	 */
	float normalY;
	/**
	 * The unit normal's z.
	 */
	float normalZ;
	/**
	 * The tangent plane's offset: the point's dot product with the
	 * normal.
	 */
	float offset;
	/**
	 * One over the distance from the pixel's tangent plane at which a
	 * neighbour one pixel away weighs 1/e, a share of the pixel's
	 * distance; zero on the sky.
	 */
	float inverseReach;
	/**
	 * 1 where the pixel shows the sky, or nothing, else 0: a fourth
	 * component of the normal, so that the sky faces the sky alone.
	 */
	float sky;
};

/**
 * Each shown pixel's guide, by rows from the top left, a buffer a field.
 */
struct DenoiseGuides {
	/**
	 * The points' x (DenoiseGuide.x).
	 */
	std::vector<float> x;
	/**
	 * The points' y.
	 */
	std::vector<float> y;
	/**
	 * The points' z.
	 */
	std::vector<float> z;
	/**
	 * The normals' x.
	 */
	std::vector<float> normalX;
	/**
	 * The normals' y.
	 */
	std::vector<float> normalY;
	/**
	 * The normals' z.
	 */
	std::vector<float> normalZ;
	/**
	 * The tangent planes' offsets.
	 */
	std::vector<float> offset;
	/**
	 * The inverse reaches (DenoiseGuide.inverseReach).
	 */
	std::vector<float> inverseReach;
	/**
	 * Whether each pixel shows the sky, as 1 or 0.
	 */
	std::vector<float> sky;
};

/**
 * A shown pixel's light between the filter's passes.
 */
struct DenoiseLight {
	/**
	 * The red channel, divided by the albedo.
	 */
	float red;
	/**
	 * The green channel, divided by the albedo.
	 */
	float green;
	/**
	 * The blue channel, divided by the albedo.
	 */
	float blue;
	/**
	 * The variance of its luminance; negative before the pixels around
	 * a pixel with a young history have estimated it.
	 */
	float variance;
};

/**
 * Each shown pixel's light between the filter's passes, by rows from
 * the top left, a buffer a field.
 */
struct DenoiseLights {
	/**
	 * The red channels, divided by the albedo.
	 */
	std::vector<float> red;
	/**
	 * The green channels, divided by the albedo.
	 */
	std::vector<float> green;
	/**
	 * The blue channels, divided by the albedo.
	 */
	std::vector<float> blue;
	/**
	 * The luminance of each pixel's three channels.
	 */
	std::vector<float> luminance;
	/**
	 * The variances (DenoiseLight.variance).
	 */
	std::vector<float> variance;
};

/**
 * What the denoiser keeps from one frame to the next: only buffers it
 * reuses, so that a frame allocates nothing but its image.
 */
struct DenoiseState {
	/**
	 * Each shown pixel's surface; its contents mean nothing between
	 * frames.
	 */
	DenoiseGuides guides;
	/**
	 * Each shown pixel's light, the input of the next pass; its contents
	 * mean nothing between frames.
	 */
	DenoiseLights light;
	/**
	 * The buffers a pass writes its light into; their contents mean
	 * nothing.
	 */
	DenoiseLights spare;
};

/**
 * A denoised frame and the buffers the next frame reuses.
 */
struct Denoised {
	/**
	 * The shown image, linear, at the shown size.
	 */
	Image image;
	/**
	 * What the next frame reuses.
	 */
	DenoiseState state;
};

/**
 * One pass of the filter over the light of the pass before.
 */
struct DenoisePass {
	/**
	 * The shown width.
	 */
	int width;
	/**
	 * The shown height.
	 */
	int height;
	/**
	 * The step between the kernel's taps, in pixels.
	 */
	int step;
	/**
	 * How many threads the pass may use.
	 */
	int threads;
};

/**
 * A run of pixels of one row that a pass works through at once.
 */
struct DenoiseSpan {
	/**
	 * The pass.
	 */
	DenoisePass pass;
	/**
	 * The row.
	 */
	int y;
	/**
	 * The first column.
	 */
	int begin;
	/**
	 * The column after the last.
	 */
	int end;
};

/**
 * One tap of a kernel: its offset from the pixel and its weight.
 */
struct DenoiseTap {
	/**
	 * The offset along the row, in pixels.
	 */
	int dx;
	/**
	 * The offset along the column, in pixels.
	 */
	int dy;
	/**
	 * The kernel's weight there.
	 */
	float kernel;
	/**
	 * Whether the tap also sums its luminance squared, for the
	 * variance's estimate.
	 */
	bool squares;
};

/**
 * A pixel of a span and one of its taps.
 */
struct DenoisePair {
	/**
	 * The pixel's place in the span.
	 */
	std::size_t k;
	/**
	 * The tap's index, by rows from the top left.
	 */
	std::size_t q;
};

/**
 * The kernel a pass sums its taps by.
 */
struct DenoiseKernel {
	/**
	 * How many steps the taps reach from the pixel along each side.
	 */
	int radius;
	/**
	 * The step between the taps, in pixels.
	 */
	int step;
	/**
	 * Whether the taps weigh the B3 spline's weights (radius 2) or all
	 * the same.
	 */
	bool spline;
	/**
	 * Whether the taps weigh less the more their luminance differs, by
	 * the pixel's prefiltered deviation; else the luminance is not
	 * compared.
	 */
	bool guided;
};

/**
 * What a pass sums over the taps of a span's pixels, by the pixels'
 * places in the span.
 */
struct DenoiseChunk {
	/**
	 * The span.
	 */
	DenoiseSpan span;
	/**
	 * The luminance of each pixel's light.
	 */
	std::array<float, denoiseChunk> luminance;
	/**
	 * Each pixel's normal's x (DenoiseGuide.normalX).
	 */
	std::array<float, denoiseChunk> normalX;
	/**
	 * Each pixel's normal's y.
	 */
	std::array<float, denoiseChunk> normalY;
	/**
	 * Each pixel's normal's z.
	 */
	std::array<float, denoiseChunk> normalZ;
	/**
	 * Whether each pixel shows the sky (DenoiseGuide.sky).
	 */
	std::array<float, denoiseChunk> sky;
	/**
	 * Each pixel's tangent plane's offset.
	 */
	std::array<float, denoiseChunk> offset;
	/**
	 * Each pixel's inverse reach over the pass's step.
	 */
	std::array<float, denoiseChunk> inversePlane;
	/**
	 * One over the luminance difference at which a tap weighs 1/e.
	 */
	std::array<float, denoiseChunk> inverseDeviation;
	/**
	 * The weighted sum of the red channel.
	 */
	std::array<float, denoiseChunk> red;
	/**
	 * The weighted sum of the green channel.
	 */
	std::array<float, denoiseChunk> green;
	/**
	 * The weighted sum of the blue channel.
	 */
	std::array<float, denoiseChunk> blue;
	/**
	 * The sum of the variances by the squared weights.
	 */
	std::array<float, denoiseChunk> variance;
	/**
	 * The weighted sum of the luminance squared, for the variance's
	 * estimate.
	 */
	std::array<float, denoiseChunk> squares;
	/**
	 * The sum of the weights.
	 */
	std::array<float, denoiseChunk> weight;
};

/**
 * The film of a lens at the size of the temporal history's grid, as the
 * headings of its pixels' rays through the pinhole eye.
 */
struct DenoiseCamera {
	/**
	 * The heading to the film's top left corner.
	 */
	math::Vector corner;
	/**
	 * The step of the heading from one column to the next.
	 */
	math::Vector across;
	/**
	 * The step of the heading from one row to the next.
	 */
	math::Vector down;
	/**
	 * Where in its pixel each pixel's ray passes (temporalCentre()).
	 */
	FilmCentre centre;
};

/**
 * Work a pass spreads over several threads.
 */
struct DenoiseRows {
	/**
	 * How many items.
	 */
	int count;
	/**
	 * How many threads.
	 */
	int threads;
	/**
	 * The work of one item, by its index.
	 */
	std::function<void(int)> row;
};

/**
 * Return the state of a denoiser that has seen no frame.
 */
DenoiseState emptyDenoise();

/**
 * Return the temporal upscaler's frame filtered against its noise: an
 * edge-avoiding à-trous filter guided by the variance (SVGF-like).
 *
 * It works on the temporal history's grid, the image at its size: the
 * drawn pixels where the history is demodulated, each looked at through
 * its jittered centre (temporalCentre()), else the shown pixels.
 *
 * The light of each pixel is divided by its albedo, channel by
 * channel (by 1 where a channel's albedo is below 1e-3, as on the sky;
 * a demodulated history holds that lighting already),
 * filtered in 4 passes of a 5x5 B3 spline kernel at steps of 1, 2, 4 and
 * 8 pixels, and multiplied by the albedo again. A tap weighs the
 * kernel's weight times its normal's cosine with the pixel's to the
 * 128th (zero below a cosine of 0.5), times exp(-plane - apart), zero
 * below exp(-30) and evaluated by a quadratic fit of 2 to the
 * fraction, within 0.3%: plane is the tap's point's distance
 * from the pixel's tangent plane over 2% of the pixel's distance times
 * the step, apart the difference of their luminance over 4 times the
 * pixel's standard deviation plus 1e-4 plus 1e-3 of the pixel's
 * luminance. The sky is filtered with the sky alone, by the luminance.
 *
 * The variance is that of the pixel's blend: where its history holds 4
 * samples or more, the temporal moment less the luminance squared, over
 * the history's length and the albedo's luminance squared (1 where the
 * history is demodulated); elsewhere the spread of the luminance of the
 * 7x7 pixels around it, weighed by the plane and the normal. Each pass
 * reads it through a 3x3 gaussian and carries it on as the sum of the
 * variances by the squared weights, over the squared sum of the
 * weights. Pixels with a long history and little spread are barely
 * changed. It is biased: it trades detail for less noise.
 *
 * The filtered light fades into the temporal light as the history grows:
 * a pixel shows the filtered share 1 / (1 + length / 8) of it, all of it
 * with no history, half at 8 samples, a fifth at 32, so that a still
 * camera keeps converging to its samples' mean instead of to the
 * filter's.
 *
 * @param before The state after the frame before, handed over so that
 *        its buffers are reused.
 * @param temporal This frame's temporal history: each shown pixel's
 *        light, moment, length, surface and albedo.
 * @param frame The frame.
 */
Denoised denoisedFrame(DenoiseState before, const TemporalState& temporal, const ShownFrame& frame);

}
