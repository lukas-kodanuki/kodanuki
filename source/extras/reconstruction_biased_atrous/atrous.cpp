#include "extras/reconstruction_biased_atrous/atrous.h"

#include "sdf/program.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace kodanuki {

constexpr int denoisePasses                   = 4;
constexpr int varianceRadius                  = 3;
constexpr float youngHistory                  = 4;
constexpr double settledLength                = 8;
constexpr double planeShare                   = 0.02;
constexpr float deviationSpread               = 4;
constexpr float luminanceFloor                = 1e-4f;
constexpr float luminanceShare                = 1e-3f;
constexpr float exponentFloor                 = -30;
constexpr float facingFloor                   = 0.5f;
constexpr std::array<float, 5> splineKernel   = { 1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16 };
constexpr std::array<float, 3> gaussianKernel = { 0.25f, 0.5f, 0.25f };

float quickExp(float x) {
	const float t     = std::max(x, exponentFloor) * 1.44269504f;
	const float whole = std::floor(t);
	const float f     = t - whole;
	const float power = 1 + f * (0.66023f + f * 0.33977f);
	const float value = std::bit_cast<float>(std::bit_cast<std::int32_t>(power) + (std::int32_t(whole) << 23));
	return x > exponentFloor ? value : 0.0f;
}

float rgbLuminance(float red, float green, float blue) {
	return 0.2126f * red + 0.7152f * green + 0.0722f * blue;
}

std::size_t denoiseIndex(int width, int x, int y) {
	return std::size_t(y) * std::size_t(width) + std::size_t(x);
}

void eachDenoiseRow(const DenoiseRows& rows) {
	std::atomic<int> next = 0;
	const auto work       = [&]() {
		for (int y = next++; y < rows.count; y = next++) {
			rows.row(y);
		}
	};

	std::vector<std::thread> threads;
	for (int k = 0; k < std::max(rows.threads, 1); ++k) {
		threads.emplace_back(work);
	}
	for (std::thread& thread : threads) {
		thread.join();
	}
}

DenoiseCamera denoiseCamera(const Lens& lens, const TemporalState& temporal) {
	const double sideways = lens.reach * lens.aspect;
	return DenoiseCamera {
		.corner = lens.forward - lens.right * sideways + lens.up * lens.reach,
		.across = lens.right * (2 * sideways / double(temporal.width)),
		.down   = lens.up * (-2 * lens.reach / double(temporal.height)),
		.centre = temporalCentre(temporal),
	};
}

math::Vector pixelHeading(const DenoiseCamera& camera, int x, int y) {
	return camera.corner + camera.across * (double(x) + camera.centre.x) + camera.down * (double(y) + camera.centre.y);
}

bool showsSky(const TemporalPixel& pixel) {
	return pixel.leaf == noLeaf || !std::isfinite(pixel.distance) || !(pixel.distance > 0);
}

DenoiseGuide skyGuide() {
	return DenoiseGuide {
		.x            = 0,
		.y            = 0,
		.z            = 0,
		.normalX      = 0,
		.normalY      = 0,
		.normalZ      = 0,
		.offset       = 0,
		.inverseReach = 0,
		.sky          = 1,
	};
}

DenoiseGuide guideOf(const TemporalPixel& pixel, const math::Vector& heading) {
	if (showsSky(pixel)) {
		return skyGuide();
	}
	const math::Vector point = heading * (double(pixel.distance) / math::length(heading));
	return DenoiseGuide {
		.x            = float(point.x),
		.y            = float(point.y),
		.z            = float(point.z),
		.normalX      = float(pixel.normal.x),
		.normalY      = float(pixel.normal.y),
		.normalZ      = float(pixel.normal.z),
		.offset       = float(math::dot(point, pixel.normal)),
		.inverseReach = float(1 / (planeShare * double(pixel.distance))),
		.sky          = 0,
	};
}

void storedGuide(DenoiseGuides& guides, std::size_t at, const DenoiseGuide& guide) {
	guides.x[at]            = guide.x;
	guides.y[at]            = guide.y;
	guides.z[at]            = guide.z;
	guides.normalX[at]      = guide.normalX;
	guides.normalY[at]      = guide.normalY;
	guides.normalZ[at]      = guide.normalZ;
	guides.offset[at]       = guide.offset;
	guides.inverseReach[at] = guide.inverseReach;
	guides.sky[at]          = guide.sky;
}

Colour lightDivisor(const TemporalPixel& pixel, bool demodulated) {
	return demodulated ? Colour { 1, 1, 1 } : lightingDivisor(pixel.albedo);
}

float historyVariance(const TemporalPixel& pixel, const Colour& divisor) {
	if (!(pixel.length >= youngHistory)) {
		return -1;
	}
	const double luminance = luminanceOf(pixel.colour);
	const double scale     = luminanceOf(divisor);
	const double spread    = std::max(pixel.moment - luminance * luminance, 0.0);
	return float(spread / (double(pixel.length) * scale * scale));
}

DenoiseLight demodulatedLight(const TemporalPixel& pixel, bool demodulated) {
	const Colour divisor = lightDivisor(pixel, demodulated);
	return DenoiseLight {
		.red      = float(pixel.colour.red / divisor.red),
		.green    = float(pixel.colour.green / divisor.green),
		.blue     = float(pixel.colour.blue / divisor.blue),
		.variance = historyVariance(pixel, divisor),
	};
}

void storedLight(DenoiseLights& lights, std::size_t at, const DenoiseLight& light) {
	lights.red[at]       = light.red;
	lights.green[at]     = light.green;
	lights.blue[at]      = light.blue;
	lights.luminance[at] = rgbLuminance(light.red, light.green, light.blue);
	lights.variance[at]  = light.variance;
}

void preparedDenoise(DenoiseState& state, const TemporalState& temporal, const ShownFrame& frame) {
	const DenoiseCamera camera = denoiseCamera(frame.lens, temporal);
	const auto row             = [&](int y) {
		for (int x = 0; x < temporal.width; ++x) {
			const std::size_t at = denoiseIndex(temporal.width, x, y);
			storedGuide(state.guides, at, guideOf(temporal.pixels[at], pixelHeading(camera, x, y)));
			storedLight(state.light, at, demodulatedLight(temporal.pixels[at], temporal.demodulated));
		}
	};

	const DenoiseRows rows = {
		.count   = temporal.height,
		.threads = frame.threads,
		.row     = row,
	};

	eachDenoiseRow(rows);
}

int chunksAcross(const DenoisePass& pass) {
	return (pass.width + denoiseChunk - 1) / denoiseChunk;
}

DenoiseSpan spanOf(const DenoisePass& pass, int item) {
	const int chunks = chunksAcross(pass);
	const int begin  = item % chunks * denoiseChunk;
	return DenoiseSpan {
		.pass  = pass,
		.y     = item / chunks,
		.begin = begin,
		.end   = std::min(begin + denoiseChunk, pass.width),
	};
}

bool youngSpan(const DenoiseLights& light, const DenoiseSpan& span) {
	for (int x = span.begin; x < span.end; ++x) {
		if (light.variance[denoiseIndex(span.pass.width, x, span.y)] < 0) {
			return true;
		}
	}
	return false;
}

void addedVarianceTap(DenoiseChunk& chunk, const DenoiseLights& light, const DenoiseTap& tap) {
	const DenoiseSpan& span = chunk.span;
	const int y             = span.y + tap.dy;
	if (y < 0 || y >= span.pass.height) {
		return;
	}
	const int begin       = std::max(span.begin, -tap.dx);
	const int end         = std::min(span.end, span.pass.width - tap.dx);
	const std::size_t row = denoiseIndex(span.pass.width, 0, y) + std::size_t(tap.dx);
	for (int x = begin; x < end; ++x) {
		const std::size_t k = std::size_t(x - span.begin);
		chunk.variance[k] += tap.kernel * light.variance[row + std::size_t(x)];
		chunk.weight[k] += tap.kernel;
	}
}

void prefilteredDeviation(DenoiseChunk& chunk, const DenoiseLights& light) {
	for (int j = 0; j < 3; ++j) {
		for (int i = 0; i < 3; ++i) {
			const DenoiseTap tap = {
				.dx      = i - 1,
				.dy      = j - 1,
				.kernel  = gaussianKernel[std::size_t(i)] * gaussianKernel[std::size_t(j)],
				.squares = false,
			};

			addedVarianceTap(chunk, light, tap);
		}
	}
	const DenoiseSpan& span = chunk.span;
	for (int x = span.begin; x < span.end; ++x) {
		const std::size_t k       = std::size_t(x - span.begin);
		const float luminance     = light.luminance[denoiseIndex(span.pass.width, x, span.y)];
		const float deviation     = std::sqrt(chunk.variance[k] / chunk.weight[k]);
		chunk.inverseDeviation[k] = 1 / (deviationSpread * deviation + luminanceFloor + luminanceShare * luminance);
		chunk.variance[k]         = 0;
		chunk.weight[k]           = 0;
	}
}

void centredSpan(DenoiseChunk& chunk, const DenoiseState& state, float inverseStep) {
	const DenoiseSpan& span     = chunk.span;
	const DenoiseGuides& guides = state.guides;
	for (int x = span.begin; x < span.end; ++x) {
		const std::size_t k   = std::size_t(x - span.begin);
		const std::size_t p   = denoiseIndex(span.pass.width, x, span.y);
		chunk.luminance[k]    = state.light.luminance[p];
		chunk.normalX[k]      = guides.normalX[p];
		chunk.normalY[k]      = guides.normalY[p];
		chunk.normalZ[k]      = guides.normalZ[p];
		chunk.sky[k]          = guides.sky[p];
		chunk.offset[k]       = guides.offset[p];
		chunk.inversePlane[k] = guides.inverseReach[p] * inverseStep;
	}
}

DenoiseChunk startedChunk(const DenoiseState& state, const DenoiseSpan& span, const DenoiseKernel& kernel) {
	DenoiseChunk chunk = {
		.span             = span,
		.luminance        = {},
		.normalX          = {},
		.normalY          = {},
		.normalZ          = {},
		.sky              = {},
		.offset           = {},
		.inversePlane     = {},
		.inverseDeviation = {},
		.red              = {},
		.green            = {},
		.blue             = {},
		.variance         = {},
		.squares          = {},
		.weight           = {},
	};

	centredSpan(chunk, state, 1 / float(kernel.step));
	if (kernel.guided) {
		prefilteredDeviation(chunk, state.light);
	}
	return chunk;
}

float facingWeight(const DenoiseChunk& chunk, const DenoiseGuides& guides, const DenoisePair& pair) {
	const std::size_t k = pair.k;
	const std::size_t q = pair.q;
	const float facing  = chunk.normalX[k] * guides.normalX[q] + chunk.normalY[k] * guides.normalY[q] + chunk.normalZ[k] * guides.normalZ[q] + chunk.sky[k] * guides.sky[q];
	float power         = facing > facingFloor ? facing : 0.0f;
	for (int n = 0; n < 7; ++n) {
		power *= power;
	}
	return power;
}

float planeExponent(const DenoiseChunk& chunk, const DenoiseGuides& guides, const DenoisePair& pair) {
	const std::size_t k = pair.k;
	const std::size_t q = pair.q;
	const float along   = guides.x[q] * chunk.normalX[k] + guides.y[q] * chunk.normalY[k] + guides.z[q] * chunk.normalZ[k];
	return std::abs(along - chunk.offset[k]) * chunk.inversePlane[k];
}

void addedTap(DenoiseChunk& chunk, const DenoiseState& state, const DenoiseTap& tap) {
	const DenoiseSpan& span = chunk.span;
	const int y             = span.y + tap.dy;
	if (y < 0 || y >= span.pass.height) {
		return;
	}
	const int begin            = std::max(span.begin, -tap.dx);
	const int end              = std::min(span.end, span.pass.width - tap.dx);
	const std::size_t row      = denoiseIndex(span.pass.width, 0, y) + std::size_t(tap.dx);
	const DenoiseLights& light = state.light;
#pragma GCC ivdep
	for (int x = begin; x < end; ++x) {
		const DenoisePair pair = {
			.k = std::size_t(x - span.begin),
			.q = row + std::size_t(x),
		};

		const std::size_t k   = pair.k;
		const float luminance = light.luminance[pair.q];
		const float apart     = std::abs(luminance - chunk.luminance[k]) * chunk.inverseDeviation[k];
		const float weight    = tap.kernel * facingWeight(chunk, state.guides, pair) * quickExp(-planeExponent(chunk, state.guides, pair) - apart);
		chunk.red[k] += weight * light.red[pair.q];
		chunk.green[k] += weight * light.green[pair.q];
		chunk.blue[k] += weight * light.blue[pair.q];
		chunk.variance[k] += weight * weight * light.variance[pair.q];
		chunk.weight[k] += weight;
		if (tap.squares) {
			chunk.squares[k] += weight * luminance * luminance;
		}
	}
}

float kernelWeight(const DenoiseKernel& kernel, int i) {
	return kernel.spline ? splineKernel[std::size_t(i + 2)] : 1.0f;
}

DenoiseChunk summedChunk(const DenoiseState& state, const DenoiseSpan& span, const DenoiseKernel& kernel) {
	DenoiseChunk chunk = startedChunk(state, span, kernel);
	for (int j = -kernel.radius; j <= kernel.radius; ++j) {
		for (int i = -kernel.radius; i <= kernel.radius; ++i) {
			const DenoiseTap tap = {
				.dx      = i * kernel.step,
				.dy      = j * kernel.step,
				.kernel  = kernelWeight(kernel, i) * kernelWeight(kernel, j),
				.squares = !kernel.guided,
			};

			addedTap(chunk, state, tap);
		}
	}
	return chunk;
}

void copiedSpan(DenoiseLights& spare, const DenoiseLights& light, const DenoiseSpan& span) {
	const std::size_t begin = denoiseIndex(span.pass.width, span.begin, span.y);
	const std::size_t end   = denoiseIndex(span.pass.width, span.end, span.y);
	for (std::size_t p = begin; p < end; ++p) {
		spare.red[p]       = light.red[p];
		spare.green[p]     = light.green[p];
		spare.blue[p]      = light.blue[p];
		spare.luminance[p] = light.luminance[p];
		spare.variance[p]  = light.variance[p];
	}
}

float estimatedVariance(const DenoiseChunk& chunk, std::size_t k, float variance) {
	if (!(variance < 0)) {
		return variance;
	}
	if (!(chunk.weight[k] > 0)) {
		return 0;
	}
	const float mean = rgbLuminance(chunk.red[k], chunk.green[k], chunk.blue[k]) / chunk.weight[k];
	return std::max(chunk.squares[k] / chunk.weight[k] - mean * mean, 0.0f);
}

void storedSpread(DenoiseLights& spare, const DenoiseChunk& chunk, const DenoiseLights& light) {
	const DenoiseSpan& span = chunk.span;
	copiedSpan(spare, light, span);
	for (int x = span.begin; x < span.end; ++x) {
		const std::size_t p = denoiseIndex(span.pass.width, x, span.y);
		spare.variance[p]   = estimatedVariance(chunk, std::size_t(x - span.begin), light.variance[p]);
	}
}

void estimatedChunk(DenoiseState& state, const DenoiseSpan& span) {
	if (!youngSpan(state.light, span)) {
		copiedSpan(state.spare, state.light, span);
		return;
	}

	const DenoiseKernel kernel = {
		.radius = varianceRadius,
		.step   = 1,
		.spline = false,
		.guided = false,
	};

	storedSpread(state.spare, summedChunk(state, span, kernel), state.light);
}

void storedChunk(DenoiseLights& spare, const DenoiseChunk& chunk, const DenoiseLights& light) {
	const DenoiseSpan& span = chunk.span;
	copiedSpan(spare, light, span);
	for (int x = span.begin; x < span.end; ++x) {
		const std::size_t p = denoiseIndex(span.pass.width, x, span.y);
		const float weight  = chunk.weight[std::size_t(x - span.begin)];
		if (!(weight > 0)) {
			continue;
		}
		const std::size_t k = std::size_t(x - span.begin);
		const float scale   = 1 / weight;
		spare.red[p]        = chunk.red[k] * scale;
		spare.green[p]      = chunk.green[k] * scale;
		spare.blue[p]       = chunk.blue[k] * scale;
		spare.luminance[p]  = rgbLuminance(spare.red[p], spare.green[p], spare.blue[p]);
		spare.variance[p]   = chunk.variance[k] * scale * scale;
	}
}

void filteredChunk(DenoiseState& state, const DenoiseSpan& span) {
	const DenoiseKernel kernel = {
		.radius = 2,
		.step   = span.pass.step,
		.spline = true,
		.guided = true,
	};

	storedChunk(state.spare, summedChunk(state, span, kernel), state.light);
}

void chunkPass(DenoiseState& state, const DenoisePass& pass, void (*chunk)(DenoiseState&, const DenoiseSpan&)) {
	const auto row = [&](int item) {
		chunk(state, spanOf(pass, item));
	};

	const DenoiseRows rows = {
		.count   = pass.height * chunksAcross(pass),
		.threads = pass.threads,
		.row     = row,
	};

	eachDenoiseRow(rows);
	std::swap(state.light, state.spare);
}

DenoisePass passOf(const TemporalState& temporal, int threads, int step) {
	return DenoisePass {
		.width   = temporal.width,
		.height  = temporal.height,
		.step    = step,
		.threads = threads,
	};
}

void filteredPasses(DenoiseState& state, const TemporalState& temporal, const ShownFrame& frame) {
	chunkPass(state, passOf(temporal, frame.threads, 1), estimatedChunk);
	for (int k = 0; k < denoisePasses; ++k) {
		chunkPass(state, passOf(temporal, frame.threads, 1 << k), filteredChunk);
	}
}

double filteredShare(const TemporalPixel& pixel) {
	return 1 / (1 + double(pixel.length) / settledLength);
}

double fadedChannel(double filtered, double held, double share) {
	return filtered * share + held * (1 - share);
}

Image remodulatedImage(const DenoiseState& state, const TemporalState& temporal) {
	Image image = blankImage(temporal.width, temporal.height);
	for (std::size_t k = 0; k < temporal.pixels.size(); ++k) {
		const TemporalPixel& pixel = temporal.pixels[k];
		const Colour divisor       = lightingDivisor(pixel.albedo);
		const Colour held          = shownLight(pixel, temporal.demodulated);
		const double share         = filteredShare(pixel);
		image.rgb[k * 3]           = float(fadedChannel(double(state.light.red[k]) * divisor.red, held.red, share));
		image.rgb[k * 3 + 1]       = float(fadedChannel(double(state.light.green[k]) * divisor.green, held.green, share));
		image.rgb[k * 3 + 2]       = float(fadedChannel(double(state.light.blue[k]) * divisor.blue, held.blue, share));
	}
	return image;
}

DenoiseGuides sizedGuides(DenoiseGuides guides, std::size_t count) {
	guides.x.resize(count);
	guides.y.resize(count);
	guides.z.resize(count);
	guides.normalX.resize(count);
	guides.normalY.resize(count);
	guides.normalZ.resize(count);
	guides.offset.resize(count);
	guides.inverseReach.resize(count);
	guides.sky.resize(count);
	return guides;
}

DenoiseLights sizedLights(DenoiseLights lights, std::size_t count) {
	lights.red.resize(count);
	lights.green.resize(count);
	lights.blue.resize(count);
	lights.luminance.resize(count);
	lights.variance.resize(count);
	return lights;
}

DenoiseState sizedDenoise(DenoiseState state, std::size_t count) {
	return DenoiseState {
		.guides = sizedGuides(std::move(state.guides), count),
		.light  = sizedLights(std::move(state.light), count),
		.spare  = sizedLights(std::move(state.spare), count),
	};
}

DenoiseState emptyDenoise() {
	return DenoiseState {
		.guides = {},
		.light  = {},
		.spare  = {},
	};
}

Denoised denoisedFrame(DenoiseState before, const TemporalState& temporal, const ShownFrame& frame) {
	DenoiseState state = sizedDenoise(std::move(before), std::size_t(temporal.width) * std::size_t(temporal.height));
	preparedDenoise(state, temporal, frame);
	filteredPasses(state, temporal, frame);
	Image image = remodulatedImage(state, temporal);
	return Denoised {
		.image = std::move(image),
		.state = std::move(state),
	};
}

}
