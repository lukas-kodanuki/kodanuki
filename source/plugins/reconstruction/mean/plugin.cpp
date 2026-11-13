#include "plugins/reconstruction/mean/plugin.h"

#include "core/frame.h"
#include "core/reconstruction.h"

#include <cstddef>

namespace kodanuki {

Image meanReconstructed(States&, Image, const ReconstructionQuery& query) {
	Image image = blankImage(query.frame.record.width, query.frame.record.height);
	for (std::size_t pixel = 0; pixel < query.frame.estimates.size(); ++pixel) {
		const Colour value       = pixelColour(query.frame.estimates[pixel], query.prepared, pixel);
		image.rgb[pixel * 3]     = float(value.red);
		image.rgb[pixel * 3 + 1] = float(value.green);
		image.rgb[pixel * 3 + 2] = float(value.blue);
	}
	return image;
}

Hooks meanHooks() {
	Hooks hooks            = noHooks();
	hooks.reconstructFrame = meanReconstructed;
	return hooks;
}

Plugin reconstructionMeanPlugin() {
	return Plugin {
		.name   = "reconstruction/mean",
		.point  = Point::Reconstruction,
		.effect = Effect::Exact,
		.hooks  = meanHooks(),
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
