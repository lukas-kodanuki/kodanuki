#include "extras/reconstruction_biased_upscale/upscale.h"
#include "check.h"
#include "extras/reconstruction_biased_upsample/upsample.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace kodanuki {

constexpr double upscaleFarPlane  = 10;
constexpr double upscaleNearPlane = 5;
constexpr double upscaleEdge      = -0.3125;

struct UpscaleWorld {
	std::function<Colour(const math::Vector&)> colour;
	double nearLeft;
	double nearRight;
};

struct UpscaleStep {
	const Lens& lens;
	const Lens& before;
	FilmPlan film;
	bool moved;
	bool restarted;
};

struct UpscaleShot {
	Image image;
	FrameRecord record;
};

struct UpscaleRun {
	UpscaleState state;
	Image image;
};

struct UpscaleHit {
	double distance;
	math::Vector point;
};

Lens upscaleLens(double eyeX, int width, int height) {
	return Lens {
		.eye      = { eyeX, 0, 0 },
		.forward  = { 0, 0, -1 },
		.right    = { 1, 0, 0 },
		.up       = { 0, 1, 0 },
		.reach    = 0.5,
		.aspect   = double(width) / double(height),
		.aperture = 0,
		.focus    = upscaleFarPlane,
		.width    = width,
		.height   = height,
	};
}

UpscaleHit upscaleHit(const UpscaleWorld& world, const Ray& ray) {
	const double near        = (ray.origin.z + upscaleNearPlane) / -ray.direction.z;
	const math::Vector front = ray.origin + ray.direction * near;
	if (front.x >= world.nearLeft && front.x < world.nearRight) {
		return UpscaleHit {
			.distance = near,
			.point    = front,
		};
	}
	const double far = (ray.origin.z + upscaleFarPlane) / -ray.direction.z;
	return UpscaleHit {
		.distance = far,
		.point    = ray.origin + ray.direction * far,
	};
}

Ray upscaleRay(const Lens& lens, double x, double y) {
	const LensDraw draw = {
		.x    = x,
		.y    = y,
		.lens = { 0.5, 0.5 },
	};

	return cameraRay(lens, draw);
}

Surface upscaleSurface(double distance) {
	return Surface {
		.distance  = float(distance),
		.normal    = { 0, 0, 1 },
		.leaf      = 1,
		.material  = 0,
		.albedo    = { 0.5, 0.5, 0.5 },
		.roughness = 1,
		.onward    = 1,
	};
}

void putColour(Image& image, std::size_t at, const Colour& colour) {
	image.rgb[at * 3]     = float(colour.red);
	image.rgb[at * 3 + 1] = float(colour.green);
	image.rgb[at * 3 + 2] = float(colour.blue);
}

UpscaleShot upscaleShot(const UpscaleWorld& world, const UpscaleStep& step) {
	const int factor = step.film.factor;
	const int width  = step.lens.width / factor;
	const int height = step.lens.height / factor;

	UpscaleShot shot = {
		.image = blankImage(width, height),

		.record = {
			.width    = width,
			.height   = height,
			.parts    = {},
			.surfaces = std::vector<Surface>(std::size_t(width) * std::size_t(height), upscaleSurface(0)),
		},
	};

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const double shownX      = double(x * factor + step.film.phase % factor) + 0.5;
			const double shownY      = double(y * factor + step.film.phase / factor) + 0.5;
			const UpscaleHit hit     = upscaleHit(world, upscaleRay(step.lens, shownX, shownY));
			const std::size_t at     = std::size_t(y) * std::size_t(width) + std::size_t(x);
			shot.record.surfaces[at] = upscaleSurface(hit.distance);
			putColour(shot.image, at, world.colour(hit.point));
		}
	}
	return shot;
}

ShownFrame upscaleShown(const UpscaleShot& shot, const UpscaleStep& step) {
	static const std::vector<Estimate> estimates;
	return ShownFrame {
		.width     = step.lens.width,
		.height    = step.lens.height,
		.lens      = step.lens,
		.before    = step.before,
		.film      = step.film,
		.moved     = step.moved,
		.restarted = step.restarted,
		.still     = 1,
		.record    = shot.record,
		.estimates = estimates,
		.surfaces  = shot.record.surfaces,
		.threads   = 3,
	};
}

Upscaled upscaleStep(UpscaleState state, const UpscaleWorld& world, const UpscaleStep& step) {
	const UpscaleShot shot = upscaleShot(world, step);
	return upscaledFrame(std::move(state), shot.image, upscaleShown(shot, step));
}

Image stretchedStep(const UpscaleWorld& world, const UpscaleStep& step) {
	const UpscaleShot shot = upscaleShot(world, step);
	return upsampledImage(shot.image, upscaleShown(shot, step));
}

Image upscaleTruth(const UpscaleWorld& world, const Lens& lens) {
	Image image = blankImage(lens.width, lens.height);
	for (int y = 0; y < lens.height; ++y) {
		for (int x = 0; x < lens.width; ++x) {
			const UpscaleHit hit = upscaleHit(world, upscaleRay(lens, double(x) + 0.5, double(y) + 0.5));
			putColour(image, std::size_t(y) * std::size_t(lens.width) + std::size_t(x), world.colour(hit.point));
		}
	}
	return image;
}

double upscaleError(const Image& image, const Image& truth) {
	double sum = 0;
	for (std::size_t k = 0; k < truth.rgb.size(); ++k) {
		sum += std::abs(double(image.rgb[k]) - double(truth.rgb[k]));
	}
	return sum / double(truth.rgb.size());
}

double columnsError(const Image& image, const Image& truth, int margin) {
	double most = 0;
	for (int y = 0; y < truth.height; ++y) {
		for (int x = margin; x < truth.width - margin; ++x) {
			const std::size_t at = (std::size_t(y) * std::size_t(truth.width) + std::size_t(x)) * 3;
			most                 = std::max(most, std::abs(double(image.rgb[at]) - double(truth.rgb[at])));
		}
	}
	return most;
}

Colour edgeColour(const math::Vector& point) {
	const double grey = point.x < upscaleEdge ? 0.1 : 0.8;
	return { grey, grey, grey };
}

Colour slopeColour(const math::Vector& point) {
	return { 0.5 + 0.08 * point.x, 0.4, 0.3 };
}

Colour twoPlanesColour(const math::Vector& point) {
	return point.z > -7 ? Colour { 0.9, 0.2, 0.1 } : Colour { 0.2, 0.3, 0.6 };
}

UpscaleWorld farWorld(const std::function<Colour(const math::Vector&)>& colour) {
	return UpscaleWorld {
		.colour    = colour,
		.nearLeft  = 0,
		.nearRight = 0,
	};
}

Image randomImage(int width, int height) {
	Image image         = blankImage(width, height);
	std::uint32_t state = 12345;
	for (float& value : image.rgb) {
		state = state * 1664525u + 1013904223u;
		value = float(state >> 8) / 16777216.0f * 3;
	}
	return image;
}

void checkUpscaleIdentity() {
	const Lens lens   = upscaleLens(0, 16, 12);
	const Image image = randomImage(16, 12);

	const UpscaleShot shot = {
		.image  = image,
		.record = emptyFrameRecord(),
	};

	const UpscaleStep step = {
		.lens      = lens,
		.before    = lens,
		.film      = stillFilm(),
		.moved     = false,
		.restarted = false,
	};

	const Upscaled upscaled = upscaledFrame(emptyUpscale(), image, upscaleShown(shot, step));
	check(upscaled.image.rgb == image.rgb, "upscale: at a factor of 1 the frame comes back as it is");
	check(upscaled.state.pixels.empty(), "upscale: at a factor of 1 no history is kept");
}

UpscaleRun stillFrames(UpscaleRun run, const UpscaleWorld& world, int frames) {
	const Lens lens = upscaleLens(0, 32, 32);
	for (int index = 0; index < frames; ++index) {
		const UpscaleStep step = {
			.lens      = lens,
			.before    = lens,
			.film      = bayerFilm(2, std::uint32_t(index)),
			.moved     = false,
			.restarted = false,
		};

		Upscaled upscaled = upscaleStep(std::move(run.state), world, step);
		run.state         = std::move(upscaled.state);
		run.image         = std::move(upscaled.image);
	}
	return run;
}

UpscaleRun emptyRun() {
	return UpscaleRun {
		.state = emptyUpscale(),
		.image = blankImage(0, 0),
	};
}

void checkUpscaleStillEdge() {
	const Lens lens          = upscaleLens(0, 32, 32);
	const UpscaleWorld world = farWorld(edgeColour);
	const Image truth        = upscaleTruth(world, lens);

	const UpscaleStep step = {
		.lens      = lens,
		.before    = lens,
		.film      = bayerFilm(2, 3),
		.moved     = false,
		.restarted = false,
	};

	const double stretched = upscaleError(stretchedStep(world, step), truth);
	const UpscaleRun early = stillFrames(emptyRun(), world, 4);
	const UpscaleRun late  = stillFrames(emptyRun(), world, 32);
	const UpscaleRun later = stillFrames(emptyRun(), world, 96);
	check(upscaleError(early.image, truth) < 0.5 * stretched, "upscale: four still phases sharpen an edge past the bilinear stretch");
	check(upscaleError(late.image, truth) < 0.02 * stretched, "upscale: a still camera converges to the edge at the output size");
	check(upscaleError(later.image, truth) < 0.02 * stretched, "upscale: a still camera stays converged");
}

void checkUpscaleSettle() {
	const Lens lens          = upscaleLens(0, 32, 32);
	const UpscaleWorld world = farWorld(edgeColour);
	const Image truth        = upscaleTruth(world, lens);

	const UpscaleStep moving = {
		.lens      = lens,
		.before    = lens,
		.film      = bayerFilm(2, 0),
		.moved     = true,
		.restarted = false,
	};

	Upscaled first = upscaleStep(emptyUpscale(), world, moving);

	const UpscaleRun run = {
		.state = std::move(first.state),
		.image = std::move(first.image),
	};

	const UpscaleRun settled = stillFrames(run, world, 4);
	check(upscaleError(settled.image, truth) < 1e-3, "upscale: four frames after the camera stops hold each pixel's own sample");
}

void checkUpscaleMoved() {
	const Lens lens          = upscaleLens(0, 32, 32);
	const Lens moved         = upscaleLens(10.0 / 32, 32, 32);
	const UpscaleWorld world = farWorld(slopeColour);
	const UpscaleRun run     = stillFrames(emptyRun(), world, 16);

	const UpscaleStep step = {
		.lens      = moved,
		.before    = lens,
		.film      = bayerFilm(2, 16),
		.moved     = true,
		.restarted = false,
	};

	const Upscaled upscaled = upscaleStep(run.state, world, step);
	const Image truth       = upscaleTruth(world, moved);
	const Image unmoved     = upscaleTruth(world, lens);
	check(columnsError(unmoved, truth, 2) > 0.02, "upscale: the camera moved by a pixel's step of the gradient");
	check(columnsError(upscaled.image, truth, 2) < 0.003, "upscale: a sideways move finds each pixel's history a pixel over");
}

double nearShare(const Image& image, int left, int right) {
	double most = 0;
	for (int y = 0; y < image.height; ++y) {
		for (int x = left; x < right; ++x) {
			const std::size_t at = (std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 3;
			most                 = std::max(most, std::abs(double(image.rgb[at]) - 0.9));
		}
	}
	return most;
}

void checkUpscaleAppearing() {
	const Lens lens      = upscaleLens(0, 32, 32);
	const UpscaleRun run = stillFrames(emptyRun(), farWorld(twoPlanesColour), 16);

	const UpscaleWorld object = {
		.colour    = twoPlanesColour,
		.nearLeft  = -1,
		.nearRight = 1,
	};

	const UpscaleStep step = {
		.lens      = lens,
		.before    = lens,
		.film      = bayerFilm(2, 16),
		.moved     = false,
		.restarted = false,
	};

	const Upscaled upscaled = upscaleStep(run.state, object, step);
	check(nearShare(upscaled.image, 13, 19) < 1e-3, "upscale: an object appearing in front drops the history behind it");
}

double meanRed(const Image& image) {
	double sum = 0;
	for (std::size_t k = 0; k < image.rgb.size(); k += 3) {
		sum += double(image.rgb[k]);
	}
	return sum / double(image.rgb.size() / 3);
}

void checkUpscaleDropped() {
	const Lens lens      = upscaleLens(0, 32, 32);
	const Lens wider     = upscaleLens(0, 48, 48);
	const UpscaleRun run = stillFrames(emptyRun(), farWorld(twoPlanesColour), 16);
	const auto grey      = [](const math::Vector&) {
		return Colour { 0.7, 0.7, 0.7 };
	};

	const UpscaleStep restart = {
		.lens      = lens,
		.before    = lens,
		.film      = bayerFilm(2, 16),
		.moved     = false,
		.restarted = true,
	};

	const UpscaleStep resized = {
		.lens      = wider,
		.before    = wider,
		.film      = bayerFilm(2, 16),
		.moved     = false,
		.restarted = false,
	};

	const Upscaled restarted = upscaleStep(run.state, farWorld(grey), restart);
	const Upscaled other     = upscaleStep(run.state, farWorld(grey), resized);
	check(std::abs(meanRed(restarted.image) - 0.7) < 1e-4, "upscale: a restart drops all the history");
	check(std::abs(meanRed(other.image) - 0.7) < 1e-4, "upscale: another output size drops all the history");
}

void checkUpscale() {
	checkUpscaleIdentity();
	checkUpscaleStillEdge();
	checkUpscaleSettle();
	checkUpscaleMoved();
	checkUpscaleAppearing();
	checkUpscaleDropped();
}

}
