#include "frame/reconstruction.h"

#include "extras/reconstruction_biased_upsample/upsample.h"
#include "frame/frame.h"

#include <cstddef>
#include <utility>

namespace kodanuki {

Image meanImage(const ReconstructionQuery& query) {
	Image image = blankImage(query.frame.record.width, query.frame.record.height);
	for (std::size_t pixel = 0; pixel < query.frame.estimates.size(); ++pixel) {
		const Colour value       = pixelColour(query.frame.estimates[pixel], query.prepared, pixel);
		image.rgb[pixel * 3]     = float(value.red);
		image.rgb[pixel * 3 + 1] = float(value.green);
		image.rgb[pixel * 3 + 2] = float(value.blue);
	}
	return image;
}

Reconstructed withImage(Reconstructed shown, Image image) {
	shown.image = std::move(image);
	return shown;
}

Reconstructed temporalReconstructed(Reconstructed shown, const ReconstructionQuery& query) {
	Temporal temporal      = temporalFrame(std::move(shown.history.temporal), query.frame);
	shown.image            = std::move(temporal.image);
	shown.history.temporal = std::move(temporal.state);
	return shown;
}

bool temporalShown(const TemporalState& temporal, const ShownFrame& frame) {
	const TemporalGrid grid = temporalGrid(frame);
	return temporal.width == grid.width && temporal.height == grid.height && temporal.pixels.size() == std::size_t(grid.width) * std::size_t(grid.height);
}

Reconstructed denoisedReconstructed(Reconstructed shown, const ReconstructionQuery& query) {
	if (!temporalShown(shown.history.temporal, query.frame)) {
		return shown;
	}
	Denoised denoised     = denoisedFrame(std::move(shown.history.denoise), shown.history.temporal, query.frame);
	shown.image           = std::move(denoised.image);
	shown.history.denoise = std::move(denoised.state);
	return shown;
}

Reconstructed upscaledReconstructed(Reconstructed shown, const ReconstructionQuery& query) {
	Upscaled upscaled     = upscaledFrame(std::move(shown.history.upscale), shown.image, query.frame);
	shown.image           = std::move(upscaled.image);
	shown.history.upscale = std::move(upscaled.state);
	return shown;
}

Reconstructed reconstructedBy(ReconstructionMember member, Reconstructed shown, const ReconstructionQuery& query) {
	switch (member) {
		case ReconstructionMember::Mean: return withImage(std::move(shown), meanImage(query));
		case ReconstructionMember::Upsample: {
			Image image = upsampledImage(shown.image, query.frame);
			return withImage(std::move(shown), std::move(image));
		}
		case ReconstructionMember::Temporal: return temporalReconstructed(std::move(shown), query);
		case ReconstructionMember::Denoise: return denoisedReconstructed(std::move(shown), query);
		case ReconstructionMember::Upscale: return upscaledReconstructed(std::move(shown), query);
	}

	return shown;
}

bool runsMember(const std::vector<ReconstructionMember>& members, ReconstructionMember member) {
	for (const ReconstructionMember listed : members) {
		if (listed == member) {
			return true;
		}
	}
	return false;
}

ReconstructionHistory keptByMembers(const std::vector<ReconstructionMember>& members, ReconstructionHistory history) {
	if (!runsMember(members, ReconstructionMember::Temporal)) {
		history.temporal = emptyTemporal();
	}
	if (!runsMember(members, ReconstructionMember::Denoise)) {
		history.denoise = emptyDenoise();
	}
	if (!runsMember(members, ReconstructionMember::Upscale)) {
		history.upscale = emptyUpscale();
	}
	return history;
}

Reconstructed reconstructedFrame(const std::vector<ReconstructionMember>& members, const ReconstructionQuery& query, ReconstructionHistory before) {
	Reconstructed shown = {
		.image   = blankImage(query.frame.record.width, query.frame.record.height),
		.history = std::move(before),
	};

	for (const ReconstructionMember member : members) {
		shown = reconstructedBy(member, std::move(shown), query);
	}
	shown.history = keptByMembers(members, std::move(shown.history));
	return shown;
}

ReconstructionHistory emptyReconstruction() {
	return ReconstructionHistory {
		.temporal = emptyTemporal(),
		.denoise  = emptyDenoise(),
		.upscale  = emptyUpscale(),
	};
}

bool readsSurfaces(const std::vector<ReconstructionMember>& members) {
	return runsMember(members, ReconstructionMember::Temporal) || runsMember(members, ReconstructionMember::Upscale);
}

std::vector<ReconstructionMember> defaultReconstruction() {
	return { ReconstructionMember::Mean };
}

}
