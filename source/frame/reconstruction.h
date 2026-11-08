#pragma once

#include "extras/reconstruction_biased_atrous/atrous.h"
#include "extras/reconstruction_biased_temporal/temporal.h"
#include "extras/reconstruction_biased_upscale/upscale.h"
#include "frame/preparation.h"
#include "frame/shown.h"
#include "frame/stopping.h"
#include "image/image.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * A step that turns a sequence's drawn frames into the shown image.
 *
 * The members run after every pixel of the frame has been traced, in the
 * order the sequence lists them, each from what the ones before it
 * show. A member reads the frame and its own history, never changes the
 * scene, the tree or the preparations, and drops its history on a Cut,
 * Lights or Scene change. A still frame runs no member.
 */
enum class ReconstructionMember : std::uint8_t {
	/**
	 * The running mean of the frames since the last change of any kind,
	 * each pixel's estimate plus what the preparations brought it, at the
	 * drawn size: on a still camera at a factor of 1, the image of a still
	 * frame of as many samples. Its state is the sequence's estimates
	 * (History.estimates), which each frame's sample continues.
	 */
	Mean,
	/**
	 * The image the members before show, stretched from the drawn size to
	 * the shown size, bilinearly (reconstruction_biased_upsample). It keeps
	 * no history.
	 */
	Upsample,
	/**
	 * A temporal accumulation (reconstruction_biased_temporal): each
	 * pixel blends this frame's samples into its history, reprojected
	 * into the moved camera. With a G-buffer it works on lighting at the
	 * drawn size, each drawn pixel looked at through its jittered centre;
	 * without one it upscales the drawn samples to the shown size. It
	 * reads the frame's records, not the image the members before show.
	 */
	Temporal,
	/**
	 * An edge-avoiding à-trous filter guided by the variance
	 * (reconstruction_biased_atrous). It runs after Temporal and filters
	 * the light Temporal keeps, by its spread; elsewhere it shows the
	 * image before it as it is.
	 */
	Denoise,
	/**
	 * A temporal upscaler from the internal (drawn) size to the output
	 * size (reconstruction_biased_upscale): the image the members before
	 * show, jittered by the film plan's phase, blended into a history at
	 * the output size. It runs last.
	 */
	Upscale,
};

/**
 * What the Reconstruction members keep from one frame to the next: one
 * field per member that keeps anything.
 */
struct ReconstructionHistory {
	/**
	 * The temporal member's history.
	 */
	TemporalState temporal;
	/**
	 * The denoiser's reused buffers.
	 */
	DenoiseState denoise;
	/**
	 * The upscaler's history.
	 */
	UpscaleState upscale;
};

/**
 * What a member reads of a frame of a sequence.
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
	const Prepared& prepared;
};

/**
 * The image the members so far show and what they keep for the next
 * frame.
 */
struct Reconstructed {
	/**
	 * The image, linear: at the drawn size after Mean, at the shown size
	 * after Upsample and Temporal.
	 */
	Image image;
	/**
	 * What the members keep, this frame's.
	 */
	ReconstructionHistory history;
};

/**
 * Run one member and return what the members so far show and keep: the
 * point's dispatch.
 *
 * A member reads its own history from what it is handed, the frame
 * before's, and replaces it with this frame's. The history is handed
 * over by value so that a member may reuse its buffers.
 *
 * @param member The member.
 * @param shown What the members before it show, and every member's
 *        history: this frame's for the members before it, the frame
 *        before's for the rest.
 * @param query The frame.
 */
Reconstructed reconstructedBy(ReconstructionMember member, Reconstructed shown, const ReconstructionQuery& query);

/**
 * Run the members in order and return what the last one shows and what
 * they all keep. A member that does not run keeps nothing: its history is
 * empty after the frame.
 *
 * @param members The members.
 * @param query The frame.
 * @param before What the members kept after the frame before.
 */
Reconstructed reconstructedFrame(const std::vector<ReconstructionMember>& members, const ReconstructionQuery& query, ReconstructionHistory before);

/**
 * Show each drawn pixel's estimate's mean plus what the preparations
 * brought it, as a still frame places it: the Mean member.
 *
 * @param query The frame.
 */
Image meanImage(const ReconstructionQuery& query);

/**
 * Return the history of members that have seen no frame.
 */
ReconstructionHistory emptyReconstruction();

/**
 * Return whether any of the members reads the G-buffer
 * (ShownFrame.surfaces): Temporal and Upscale do, and Denoise through
 * Temporal.
 *
 * @param members The members.
 */
bool readsSurfaces(const std::vector<ReconstructionMember>& members);

/**
 * Return the members a sequence runs by default: Mean alone.
 */
std::vector<ReconstructionMember> defaultReconstruction();

}
