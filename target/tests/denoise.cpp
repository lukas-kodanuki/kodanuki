#include "check.h"
#include "math/random.h"
#include "plugins/reconstruction/atrous/atrous.h"
#include "sdf/program.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace kodanuki {

constexpr int denoiseWidth  = 64;
constexpr int denoiseHeight = 48;
constexpr int edgeColumn    = 32;

struct DenoiseField {
	int width;
	int height;
	std::function<TemporalPixel(int, int)> pixel;
};

Lens denoiseLens(int width, int height) {
	return Lens {
		.eye      = { 0, 0, 0 },
		.forward  = { 0, 0, -1 },
		.right    = { 1, 0, 0 },
		.up       = { 0, 1, 0 },
		.reach    = 0.5,
		.aspect   = double(width) / double(height),
		.aperture = 0,
		.focus    = 10,
		.width    = width,
		.height   = height,
	};
}

double denoiseNoise(int x, int y, std::uint64_t seed) {
	const std::uint64_t bits = math::mixedBits(std::uint64_t(x) | std::uint64_t(y) << 20 | seed << 40);
	return double(bits >> 11) * 0x1.0p-52 - 1;
}

double depthDistance(const Lens& lens, int x, int y, double depth) {
	const double sideways = lens.reach * lens.aspect;
	const double across   = (2 * (double(x) + 0.5) / double(lens.width) - 1) * sideways;
	const double down     = (1 - 2 * (double(y) + 0.5) / double(lens.height)) * lens.reach;
	return depth * std::sqrt(1 + across * across + down * down);
}

TemporalPixel denoisePixel(const Colour& colour, double extra, float length) {
	const double luminance = luminanceOf(colour);
	return TemporalPixel {
		.colour   = colour,
		.moment   = luminance * luminance + extra,
		.length   = length,
		.distance = 10,
		.normal   = { 0, 0, 1 },
		.leaf     = 1,
		.albedo   = { 0, 0, 0 },
	};
}

TemporalState denoiseState(const DenoiseField& field) {
	TemporalState state = emptyTemporal();
	state.width         = field.width;
	state.height        = field.height;
	for (int y = 0; y < field.height; ++y) {
		for (int x = 0; x < field.width; ++x) {
			state.pixels.push_back(field.pixel(x, y));
		}
	}
	return state;
}

Denoised denoiseRun(DenoiseState before, const TemporalState& temporal) {
	static const FrameRecord record = emptyFrameRecord();
	static const std::vector<PixelSum> estimates;
	const Lens lens = denoiseLens(temporal.width, temporal.height);

	const ShownFrame frame = {
		.width  = temporal.width,
		.height = temporal.height,
		.lens   = lens,
		.before = lens,

		.film = {
			.factor = 1,
			.phase  = 0,
		},

		.moved     = false,
		.restarted = false,
		.recounted = false,
		.still     = 1,
		.record    = record,
		.estimates = estimates,
		.surfaces  = noSurfaces(),
		.threads   = 3,
	};

	return denoisedFrame(std::move(before), temporal, frame);
}

double redError(const Image& image, const std::function<double(int, int)>& want) {
	double sum = 0;
	for (int y = 0; y < image.height; ++y) {
		for (int x = 0; x < image.width; ++x) {
			const double apart = double(image.rgb[(std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 3]) - want(x, y);
			sum += apart * apart;
		}
	}
	return std::sqrt(sum / double(image.width * image.height));
}

double columnMean(const Image& image, int x) {
	double sum = 0;
	for (int y = 0; y < image.height; ++y) {
		sum += double(image.rgb[(std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 3]);
	}
	return sum / double(image.height);
}

Image temporalImage(const TemporalState& temporal) {
	Image image = blankImage(temporal.width, temporal.height);
	for (std::size_t k = 0; k < temporal.pixels.size(); ++k) {
		image.rgb[k * 3]     = float(temporal.pixels[k].colour.red);
		image.rgb[k * 3 + 1] = float(temporal.pixels[k].colour.green);
		image.rgb[k * 3 + 2] = float(temporal.pixels[k].colour.blue);
	}
	return image;
}

TemporalState noisyPlane(int width, int height) {
	const auto pixel = [](int x, int y) {
		const double value = 0.5 + 0.3 * denoiseNoise(x, y, 1);
		return denoisePixel({ value, value, value }, 0, 1);
	};

	const DenoiseField field = {
		.width  = width,
		.height = height,
		.pixel  = pixel,
	};

	return denoiseState(field);
}

void checkNoiseFlattened() {
	const TemporalState temporal = noisyPlane(denoiseWidth, denoiseHeight);
	const auto flat              = [](int, int) {
		return 0.5;
	};

	const double before = redError(temporalImage(temporal), flat);
	const double after  = redError(denoiseRun(emptyDenoise(), temporal).image, flat);
	check(after < before / 5, "denoise: the noise on one plane is flattened");
}

TemporalState edgeField(bool tilted) {
	const Lens lens  = denoiseLens(denoiseWidth, denoiseHeight);
	const auto pixel = [&](int x, int y) {
		const bool right     = x >= edgeColumn;
		const double value   = (right ? 0.6 : 0.4) + 0.3 * denoiseNoise(x, y, 2);
		TemporalPixel result = denoisePixel({ value, value, value }, 0, 1);
		result.distance      = float(depthDistance(lens, x, y, right && !tilted ? 12 : 10));
		result.normal        = right && tilted ? math::Vector { 0.8, 0, 0.6 } : math::Vector { 0, 0, 1 };
		return result;
	};

	const DenoiseField field = {
		.width  = denoiseWidth,
		.height = denoiseHeight,
		.pixel  = pixel,
	};

	return denoiseState(field);
}

void checkEdgeKept(bool tilted) {
	const Image image = denoiseRun(emptyDenoise(), edgeField(tilted)).image;
	check(std::abs(columnMean(image, edgeColumn - 1) - 0.4) < 0.03, "denoise: the pixels left of the edge keep their side's light");
	check(std::abs(columnMean(image, edgeColumn) - 0.6) < 0.03, "denoise: the pixels right of the edge keep their side's light");
}

double checkerAlbedo(int x, int y) {
	return (x / 4 + y / 4) % 2 == 0 ? 0.2 : 0.8;
}

void checkCheckerKept() {
	const auto pixel = [](int x, int y) {
		const double albedo  = checkerAlbedo(x, y);
		const double value   = albedo * (1 + 0.3 * denoiseNoise(x, y, 3));
		TemporalPixel result = denoisePixel({ value, value, value }, 0, 1);
		result.albedo        = { albedo, albedo, albedo };
		return result;
	};

	const DenoiseField field = {
		.width  = denoiseWidth,
		.height = denoiseHeight,
		.pixel  = pixel,
	};

	const TemporalState temporal = denoiseState(field);
	const double before          = redError(temporalImage(temporal), checkerAlbedo);
	const double after           = redError(denoiseRun(emptyDenoise(), temporal).image, checkerAlbedo);
	check(after < before / 4, "denoise: a checkered albedo keeps its checks while its light is filtered");
}

TemporalState checkerField(bool demodulated) {
	const auto pixel = [=](int x, int y) {
		const double albedo  = checkerAlbedo(x, y);
		const double light   = 1 + 0.3 * denoiseNoise(x, y, 4);
		const double scale   = demodulated ? 1 : albedo;
		const double value   = light * scale;
		TemporalPixel result = denoisePixel({ value, value, value }, 0.0025 * scale * scale, 8);
		result.albedo        = { albedo, albedo, albedo };
		return result;
	};

	const DenoiseField field = {
		.width  = denoiseWidth,
		.height = denoiseHeight,
		.pixel  = pixel,
	};

	TemporalState state = denoiseState(field);
	state.demodulated   = demodulated;
	return state;
}

void checkDemodulatedAlike() {
	const Image light   = denoiseRun(emptyDenoise(), checkerField(false)).image;
	const Image divided = denoiseRun(emptyDenoise(), checkerField(true)).image;
	double largest      = 0;
	for (std::size_t k = 0; k < light.rgb.size(); ++k) {
		largest = std::max(largest, std::abs(double(divided.rgb[k]) - double(light.rgb[k])));
	}
	check(largest < 1e-4, "denoise: a history of lighting shows what the same history of light shows");
}

void checkConvergedKept() {
	const auto pixel = [](int x, int y) {
		const double value = 0.5 + 0.3 * std::sin(double(x) / 5) * std::cos(double(y) / 7);
		return denoisePixel({ value, value, value }, 0.0025, 256);
	};

	const DenoiseField field = {
		.width  = denoiseWidth,
		.height = denoiseHeight,
		.pixel  = pixel,
	};

	const TemporalState temporal = denoiseState(field);
	const Image before           = temporalImage(temporal);
	const Image after            = denoiseRun(emptyDenoise(), temporal).image;
	double largest               = 0;
	for (std::size_t k = 0; k < before.rgb.size(); ++k) {
		largest = std::max(largest, std::abs(double(after.rgb[k]) - double(before.rgb[k])));
	}
	check(largest < 0.01, "denoise: a converged field comes out nearly unchanged");
}

void checkStateReused() {
	const Denoised first  = denoiseRun(emptyDenoise(), noisyPlane(80, 40));
	const Denoised second = denoiseRun(first.state, edgeField(false));
	const Denoised fresh  = denoiseRun(emptyDenoise(), edgeField(false));
	check(second.image.rgb == fresh.image.rgb, "denoise: a frame through the handed-over state matches one through fresh state");
}

Denoised halvedRun(const TemporalState& temporal) {
	static const FrameRecord record = emptyFrameRecord();
	static const std::vector<PixelSum> estimates;
	const Lens lens = denoiseLens(temporal.width * 2, temporal.height * 2);

	const ShownFrame frame = {
		.width     = temporal.width * 2,
		.height    = temporal.height * 2,
		.lens      = lens,
		.before    = lens,
		.film      = temporal.film,
		.moved     = false,
		.restarted = false,
		.recounted = false,
		.still     = 1,
		.record    = record,
		.estimates = estimates,
		.surfaces  = noSurfaces(),
		.threads   = 3,
	};

	return denoisedFrame(emptyDenoise(), temporal, frame);
}

void checkDrawnSize() {
	const FilmPlan film = {
		.factor = 2,
		.phase  = 3,
	};

	TemporalState temporal = noisyPlane(denoiseWidth / 2, denoiseHeight / 2);
	temporal.demodulated   = true;
	temporal.film          = film;

	const auto flat = [](int, int) {
		return 0.5;
	};

	const Image image   = halvedRun(temporal).image;
	const bool drawn    = image.width == temporal.width && image.height == temporal.height;
	const double before = redError(temporalImage(temporal), flat);
	check(drawn && redError(image, flat) < before / 4, "denoise: at factor 2 a demodulated history is filtered at its drawn size");
}

void checkDenoise() {
	checkNoiseFlattened();
	checkEdgeKept(false);
	checkEdgeKept(true);
	checkCheckerKept();
	checkDemodulatedAlike();
	checkConvergedKept();
	checkStateReused();
	checkDrawnSize();
}

}
