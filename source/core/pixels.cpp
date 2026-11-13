#include "core/pixels.h"

namespace kodanuki {

PixelSample groupSample(const PixelJob& job, int offset) {
	return PixelSample {
		.x        = job.x,
		.y        = job.y,
		.index    = std::uint32_t(job.first + offset),
		.group    = std::uint32_t(job.group + offset / job.split),
		.branches = std::uint32_t(job.split),
	};
}

PixelSlot idleSlot() {
	return PixelSlot {
		.job      = noJob,
		.estimate = emptyEstimate(),
		.launched = 0,
		.batch    = 0,
		.owing    = 0,
	};
}

}
