#pragma once

#include "frame/shown.h"
#include "image/image.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * What the upscaler keeps of one output pixel.
 */
struct UpscalePixel {
	/**
	 * The pixel's light, linear.
	 */
	Colour colour;
	/**
	 * The distance from the lens to what the pixel shows, the closest of
	 * the internal 3x3 it was made from; infinite on the sky.
	 */
	float depth;
};

/**
 * The upscaler's history: every output pixel of the last frame.
 */
struct UpscaleState {
	/**
	 * The output width; zero before the first frame.
	 */
	int width;
	/**
	 * The output height.
	 */
	int height;
	/**
	 * The pixels, by rows from the top left.
	 */
	std::vector<UpscalePixel> pixels;
	/**
	 * The buffer of the frame before's pixels, which the next frame
	 * writes into instead of allocating anew; its contents mean nothing.
	 */
	std::vector<UpscalePixel> spare;
	/**
	 * Whether the camera moved in the last frame.
	 */
	bool moved;
	/**
	 * How many frames after the camera stopped still settle: their own
	 * samples replace the moving frames' soft history.
	 */
	int settling;
};

/**
 * An upscaled frame and the history after it.
 */
struct Upscaled {
	/**
	 * The image at the output size, linear.
	 */
	Image image;
	/**
	 * The history the next frame reads.
	 */
	UpscaleState state;
};

/**
 * Return the history of no frame.
 */
UpscaleState emptyUpscale();

/**
 * Return a frame drawn at the internal size, jittered, blended into the
 * output size over time: a temporal upscaler (the GPU engine's
 * upscale.comp on the CPU).
 *
 * The internal frame is the drawn size; its pixel (x, y) shows the output
 * pixel its film plan's phase names in its factor x factor block (the
 * jitter), so every output pixel gets its own sample once every factor²
 * frames. Each output pixel reads the internal 3x3 around it, finds last
 * frame's output where its point was (the closest depth of the 3x3 along
 * its unjittered ray, through the camera before), keeps that history
 * where its depth matches, clamped to the 3x3's colour box (wide while
 * still, tight while moving), and blends in the samples near it by how
 * near they land; where the history is dropped it shows the 3x3
 * upsampled. At a factor of 1 the frame comes back as it is, and no
 * history is kept.
 *
 * The blend works in a reversible tonemap (each channel over one plus
 * the largest) and in YCoCg, so a bright highlight neither rings nor
 * drags its neighbours. A camera that stops settles for 4 frames: a
 * pixel's own sample replaces the moving frames' soft history. A
 * restart, or another output size, drops the history. Where the
 * G-buffer is not at the internal size, every sample counts as the sky
 * and the history is found by direction alone.
 *
 * @param before The history after the frame before, handed over so that
 *        its spare buffer holds this frame's pixels.
 * @param internal The frame at the internal (drawn) size, as the members
 *        before show it.
 * @param frame The frame: the output size and lenses, the film plan,
 *        and the internal G-buffer's distances (frame.surfaces).
 */
Upscaled upscaledFrame(UpscaleState before, const Image& internal, const ShownFrame& frame);

}
