#pragma once

#include "core/batch.h"
#include "core/camera.h"
#include "core/stopping.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * What a frame's paths left, at the drawn size, by rows from the top
 * left. A frame fills the light and the surfaces only where a chosen
 * Reconstruction member reads them (readsRecords()).
 */
struct FrameRecord {
	/**
	 * The frame's width.
	 */
	int width;
	/**
	 * The frame's height.
	 */
	int height;
	/**
	 * The mean light of the samples each pixel drew in the frame: its
	 * one sample's where it drew one.
	 */
	std::vector<Parts> parts;
	/**
	 * What the camera ray of each pixel's last sample met first.
	 */
	std::vector<Surface> surfaces;
};

/**
 * What a Reconstruction member reads of a frame of a sequence: how the
 * drawn pixels cover the shown ones, and what they left.
 *
 * A frame is drawn at the shown size divided by the film plan's factor.
 * Drawn pixel (x, y) aims its ray into shown pixel (x * factor + column,
 * y * factor + row), where column and row are the phase's place in the
 * factor x factor block, at a random point inside that shown pixel; at a
 * factor of 1 the drawn and shown pixels are the same.
 */
struct ShownFrame {
	/**
	 * The shown width: the drawn width times the factor.
	 */
	int width;
	/**
	 * The shown height: the drawn height times the factor.
	 */
	int height;
	/**
	 * This frame's lens, at the shown size.
	 */
	const Lens& lens;
	/**
	 * The frame before's lens, at the shown size; this frame's own on the
	 * first frame.
	 */
	const Lens& before;
	/**
	 * How the drawn pixels cover the shown ones.
	 */
	FilmPlan film;
	/**
	 * Whether the camera moved or the scene changed since the frame
	 * before (Moved, Lights, Looks or Shapes): the members keep their
	 * histories where the surface they saw stays, with a short cap and a
	 * tight clamp.
	 */
	bool moved;
	/**
	 * Whether the sequence starts over (Cut): every member drops its
	 * history.
	 */
	bool restarted;
	/**
	 * Whether the count of the scene's shapes differs from the frame
	 * before's, so a scene leaf of one frame names another shape in the
	 * next: a member then tells surfaces apart by distance and normal
	 * alone.
	 */
	bool recounted;
	/**
	 * How many frames in a row nothing changed in, this one included.
	 */
	std::uint32_t still;
	/**
	 * What this frame's samples left, at the drawn size.
	 */
	const FrameRecord& record;
	/**
	 * What each drawn pixel's samples since the last change add up to,
	 * this frame's folded in.
	 */
	const std::vector<PixelSum>& estimates;
	/**
	 * Each drawn pixel's first surface, exact for this frame, by rows
	 * from the top left at the drawn size (the G-buffer): drawn pixel
	 * (x, y)'s is met by the ray from the lens's centre through the
	 * centre of the shown pixel (x * factor + column, y * factor + row)
	 * its phase aims at (tracedSurfaces()). Empty where no Reconstruction
	 * member reads them (readsSurfaces()).
	 */
	const std::vector<Surface>& surfaces;
	/**
	 * How many threads a member may use.
	 */
	int threads;
};

/**
 * Return a lens at the shown size of a film plan: the same camera with
 * its width and height times the factor.
 *
 * @param drawn The lens at the drawn size.
 * @param film The film plan.
 */
Lens shownLens(const Lens& drawn, const FilmPlan& film);

/**
 * Return the shown pixel a drawn pixel's ray aims into this frame, by
 * rows from the top left of the shown frame.
 *
 * @param frame The frame.
 * @param drawn The drawn pixel, by rows from the top left of the drawn
 *        frame.
 */
std::size_t shownPixelOf(const ShownFrame& frame, std::size_t drawn);

/**
 * Return the film plan of a sequence's frame: the factor, and the phase
 * the frame's index takes in the Bayer order of the factor x factor
 * block (0, 3, 1, 2 at a factor of 2; 0, 7, 5, 2, 6, 4, 3, 1, 8 at 3),
 * so every shown pixel is aimed at
 * once in every factor x factor frames and the frames between spread
 * evenly over the block.
 *
 * @param factor The factor: 1 to 4.
 * @param index The frame's index in the sequence.
 */
FilmPlan bayerFilm(int factor, std::uint32_t index);

/**
 * Return a G-buffer of no pixels, for a frame that traces none; it lives
 * as long as the program.
 */
const std::vector<Surface>& noSurfaces();

/**
 * Return a record of no frame: zero size, nothing in it.
 */
FrameRecord emptyFrameRecord();

}
