#include "check.h"
#include "plugins/reconstruction/temporal/temporal.h"
#include "plugins/transport/paths/material/material.h"
#include "sdf/program.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace kodanuki {

constexpr double planeDistance = 10;

struct TestShot {
	const Lens& lens;
	const Lens& before;
	FilmPlan film;
	bool moved;
	bool restarted;
	const FrameRecord& record;
	const std::vector<Surface>& surfaces;
};

struct TestScene {
	const Lens& lens;
	FilmPlan film;
	std::function<Surface(int, int)> surface;
	std::function<Colour(int, int)> lighting;
};

struct TestPlane {
	const Lens& lens;
	FilmPlan film;
	int width;
	int height;
};

Lens temporalLens(const math::Vector& eye, int width, int height) {
	return Lens {
		.eye      = eye,
		.forward  = { 0, 0, -1 },
		.right    = { 1, 0, 0 },
		.up       = { 0, 1, 0 },
		.reach    = 0.5,
		.aspect   = double(width) / double(height),
		.aperture = 0,
		.focus    = planeDistance,
		.width    = width,
		.height   = height,
	};
}

Temporal shotTemporal(const TemporalState& before, const TestShot& shot) {
	static const std::vector<PixelSum> estimates;

	const ShownFrame frame = {
		.width     = shot.record.width * shot.film.factor,
		.height    = shot.record.height * shot.film.factor,
		.lens      = shot.lens,
		.before    = shot.before,
		.film      = shot.film,
		.moved     = shot.moved,
		.restarted = shot.restarted,
		.recounted = false,
		.still     = 1,
		.record    = shot.record,
		.estimates = estimates,
		.surfaces  = shot.surfaces,
		.threads   = 3,
	};

	return temporalFrame(before, frame);
}

Surface testSurface(double distance, std::uint32_t leaf) {
	return Surface {
		.distance  = float(distance),
		.normal    = { 0, 0, 1 },
		.leaf      = leaf,
		.material  = 0,
		.albedo    = { 0.5, 0.5, 0.5 },
		.roughness = 1,
		.onward    = 1,
	};
}

FrameRecord blankRecord(int width, int height) {
	const std::size_t count = std::size_t(width) * std::size_t(height);

	const Parts black = {
		.total = { 0, 0, 0 },
	};

	return FrameRecord {
		.width    = width,
		.height   = height,
		.parts    = std::vector<Parts>(count, black),
		.surfaces = std::vector<Surface>(count, testSurface(planeDistance, 1)),
	};
}

double noiseAt(int frame, std::size_t pixel) {
	std::uint32_t h = std::uint32_t(frame) * 2654435761u ^ std::uint32_t(pixel) * 2246822519u;
	h ^= h >> 15;
	h *= 2654435769u;
	h ^= h >> 13;
	return double(h) / 4294967296.0;
}

double meanError(const Image& image, const Colour& truth) {
	double sum               = 0;
	const std::size_t pixels = image.rgb.size() / 3;
	for (std::size_t k = 0; k < pixels; ++k) {
		sum += std::abs(double(image.rgb[k * 3]) - truth.red) + std::abs(double(image.rgb[k * 3 + 2]) - truth.blue);
	}
	return sum / double(pixels);
}

FrameRecord noisyRecord(const Colour& truth, int frame) {
	FrameRecord record = blankRecord(8, 8);
	for (std::size_t k = 0; k < record.parts.size(); ++k) {
		record.parts[k].total = truth * (2 * noiseAt(frame, k));
	}
	return record;
}

bool everyLength(const TemporalState& state, float length) {
	for (const TemporalPixel& pixel : state.pixels) {
		if (pixel.length != length) {
			return false;
		}
	}
	return true;
}

void checkStillConvergence() {
	const Colour truth  = { 0.6, 0.3, 0.1 };
	const Lens lens     = temporalLens({ 0, 0, 0 }, 8, 8);
	TemporalState state = emptyTemporal();
	double early        = 0;
	double late         = 0;
	for (int frame = 0; frame < 200; ++frame) {
		const FrameRecord record = noisyRecord(truth, frame);

		const TestShot shot = {
			.lens      = lens,
			.before    = lens,
			.film      = stillFilm(),
			.moved     = false,
			.restarted = frame == 0,
			.record    = record,
			.surfaces  = noSurfaces(),
		};

		const Temporal temporal = shotTemporal(state, shot);
		state                   = temporal.state;
		early                   = frame == 3 ? meanError(temporal.image, truth) : early;
		late                    = meanError(temporal.image, truth);
	}
	check(late < 0.3 * early, "temporal: a still camera's noisy samples converge to their mean");
	check(everyLength(state, 200), "temporal: a still pixel's history grows by a sample a frame past the moving cap");
}

Colour shownValue(int x, int y) {
	return Colour { 0.1 + 0.01 * x, 0.1 + 0.01 * y, 0.3 };
}

FrameRecord phasedRecord(const FilmPlan& film) {
	FrameRecord record = blankRecord(8, 8);
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			record.parts[std::size_t(y) * 8 + std::size_t(x)].total = shownValue(x * 2 + film.phase % 2, y * 2 + film.phase / 2);
		}
	}
	return record;
}

int emptyPixels(const TemporalState& state) {
	int count = 0;
	for (const TemporalPixel& pixel : state.pixels) {
		count += pixel.length == 0 ? 1 : 0;
	}
	return count;
}

bool holdsOwnSamples(const Temporal& temporal) {
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			const std::size_t at = std::size_t(y) * 16 + std::size_t(x);
			if (std::abs(double(temporal.image.rgb[at * 3]) - shownValue(x, y).red) > 1e-6 || std::abs(double(temporal.image.rgb[at * 3 + 1]) - shownValue(x, y).green) > 1e-6) {
				return false;
			}
		}
	}
	return true;
}

void checkPhasesFill() {
	const Lens lens     = temporalLens({ 0, 0, 0 }, 16, 16);
	TemporalState state = emptyTemporal();

	Temporal temporal = {
		.image = blankImage(0, 0),
		.state = emptyTemporal(),
	};

	for (int phase = 0; phase < 4; ++phase) {
		const FilmPlan film = {
			.factor = 2,
			.phase  = phase,
		};

		const FrameRecord record = phasedRecord(film);

		const TestShot shot = {
			.lens      = lens,
			.before    = lens,
			.film      = film,
			.moved     = false,
			.restarted = phase == 0,
			.record    = record,
			.surfaces  = noSurfaces(),
		};

		temporal = shotTemporal(state, shot);
		state    = temporal.state;
		if (phase == 0) {
			check(emptyPixels(state) == 192, "temporal: a first frame at factor 2 fills three shown pixels in four");
		}
	}
	check(emptyPixels(state) == 0, "temporal: four still phases at factor 2 leave every shown pixel a sample");
	check(holdsOwnSamples(temporal), "temporal: each shown pixel holds the sample aimed into it");
}

double gradientRed(double worldX) {
	return 0.5 + 0.1 * worldX;
}

double gradientGreen(double worldY) {
	return 0.3 + 0.05 * worldY;
}

math::Vector testHeading(const Lens& lens, int x, int y) {
	const double u = 2 * (double(x) + 0.5) / double(lens.width) - 1;
	const double v = 1 - 2 * (double(y) + 0.5) / double(lens.height);
	return lens.forward + lens.right * (u * lens.reach * lens.aspect) + lens.up * (v * lens.reach);
}

Colour planeColour(const Lens& lens, int x, int y) {
	const math::Vector point = lens.eye + testHeading(lens, x, y) * planeDistance;
	return Colour { gradientRed(point.x), gradientGreen(point.y), 0.2 };
}

bool inLeafBlock(int x, int y) {
	return x >= 2 && x < 8 && y >= 4 && y < 10;
}

bool inFarBlock(int x, int y) {
	return x >= 14 && x < 20 && y >= 4 && y < 10;
}

FrameRecord planeRecord(const TestPlane& plane) {
	FrameRecord record = blankRecord(plane.width, plane.height);
	const int factor   = plane.film.factor;
	for (int y = 0; y < plane.height; ++y) {
		for (int x = 0; x < plane.width; ++x) {
			const std::size_t at   = std::size_t(y) * std::size_t(plane.width) + std::size_t(x);
			const int shownX       = x * factor + (factor > 1 ? plane.film.phase % factor : 0);
			const int shownY       = y * factor + (factor > 1 ? plane.film.phase / factor : 0);
			const double length    = math::length(testHeading(plane.lens, shownX, shownY));
			const bool far         = factor > 1 && inFarBlock(x, y);
			const bool otherLeaf   = factor > 1 && inLeafBlock(x, y);
			record.parts[at].total = planeColour(plane.lens, shownX, shownY);
			record.surfaces[at]    = testSurface(planeDistance * length * (far ? 2 : 1), otherLeaf ? 9 : 7);
		}
	}
	return record;
}

bool nearBlock(int x, int y) {
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			if (inLeafBlock(x + dx, y + dy) || inFarBlock(x + dx, y + dy)) {
				return true;
			}
		}
	}
	return false;
}

bool landsOnPlane(const Temporal& temporal, const Lens& lens) {
	for (int y = 2; y < 30; ++y) {
		for (int x = 2; x < 44; ++x) {
			if (nearBlock((x + 1) / 2, (y + 1) / 2)) {
				continue;
			}
			const std::size_t at = std::size_t(y) * 48 + std::size_t(x);
			const Colour truth   = planeColour(lens, x, y);
			const bool red       = std::abs(double(temporal.image.rgb[at * 3]) - truth.red) < 1e-5;
			const bool green     = std::abs(double(temporal.image.rgb[at * 3 + 1]) - truth.green) < 1e-5;
			const float length   = temporal.state.pixels[at].length;
			if (!red || !green || length != (x % 2 == 0 && y % 2 == 0 ? 2.0f : 1.0f)) {
				return false;
			}
		}
	}
	return true;
}

bool droppedIn(const TemporalState& state, int fromX, int toX) {
	for (int y = 9; y <= 16; ++y) {
		for (int x = fromX; x <= toX; ++x) {
			const TemporalPixel& pixel = state.pixels[std::size_t(y) * 48 + std::size_t(x)];
			if (pixel.length != (x % 2 == 0 && y % 2 == 0 ? 1.0f : 0.0f)) {
				return false;
			}
		}
	}
	return true;
}

float longest(const TemporalState& state) {
	float most = 0;
	for (const TemporalPixel& pixel : state.pixels) {
		most = std::max(most, pixel.length);
	}
	return most;
}

void checkReprojection() {
	const Lens first        = temporalLens({ 0, 0, 0 }, 48, 32);
	const double pixelWidth = 2 * first.reach * planeDistance / 32;
	const Lens second       = temporalLens({ 1.25 * pixelWidth, 0, 0 }, 48, 32);

	const TestPlane still = {
		.lens   = first,
		.film   = stillFilm(),
		.width  = 48,
		.height = 32,
	};

	const FrameRecord before = planeRecord(still);

	const TestShot opening = {
		.lens      = first,
		.before    = first,
		.film      = stillFilm(),
		.moved     = false,
		.restarted = true,
		.record    = before,
		.surfaces  = noSurfaces(),
	};

	const Temporal start = shotTemporal(emptyTemporal(), opening);

	const FilmPlan film = {
		.factor = 2,
		.phase  = 0,
	};

	const TestPlane moved = {
		.lens   = second,
		.film   = film,
		.width  = 24,
		.height = 16,
	};

	const FrameRecord after = planeRecord(moved);

	const TestShot step = {
		.lens      = second,
		.before    = first,
		.film      = film,
		.moved     = true,
		.restarted = false,
		.record    = after,
		.surfaces  = noSurfaces(),
	};

	const Temporal next = shotTemporal(start.state, step);
	check(landsOnPlane(next, second), "temporal: a sideways step carries the history to where its points show");
	check(droppedIn(next.state, 5, 12), "temporal: a pixel showing another leaf drops its history");
	check(droppedIn(next.state, 29, 36), "temporal: a pixel whose distance jumps drops its history");

	TestShot again  = step;
	again.restarted = true;
	check(longest(next.state) == 2 && longest(shotTemporal(next.state, again).state) == 1, "temporal: a restart drops every history");
	const Lens small = temporalLens({ 0, 0, 0 }, 24, 16);

	const TestShot resized = {
		.lens      = small,
		.before    = small,
		.film      = stillFilm(),
		.moved     = false,
		.restarted = false,
		.record    = after,
		.surfaces  = noSurfaces(),
	};

	check(longest(shotTemporal(next.state, resized).state) == 1, "temporal: a frame of another size starts without history");
}

Surface albedoSurface(int x, int y) {
	if (x >= 6) {
		return Surface {
			.distance  = std::numeric_limits<float>::infinity(),
			.normal    = { 0, 0, 0 },
			.leaf      = noLeaf,
			.material  = noMaterial,
			.albedo    = { 0, 0, 0 },
			.roughness = 1,
			.onward    = std::numeric_limits<float>::infinity(),
		};
	}
	Surface surface = testSurface(planeDistance, 7);
	surface.albedo  = Colour { 0.1 + 0.05 * x, 0.1 + 0.05 * y, 0.7 };
	return surface;
}

FrameRecord albedoRecord() {
	FrameRecord record = blankRecord(8, 8);
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			record.surfaces[std::size_t(y) * 8 + std::size_t(x)] = albedoSurface(x, y);
		}
	}
	return record;
}

bool sameAlbedo(const Colour& a, const Colour& b) {
	return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

bool aimedAlbedos(const TemporalState& state, const FrameRecord& record, const FilmPlan& film) {
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 6; ++x) {
			const std::size_t shown = std::size_t(y * 2 + film.phase / 2) * 16 + std::size_t(x * 2 + film.phase % 2);
			if (!sameAlbedo(state.pixels[shown].albedo, record.surfaces[std::size_t(y) * 8 + std::size_t(x)].albedo)) {
				return false;
			}
		}
	}
	return true;
}

bool skyAlbedos(const TemporalState& state) {
	const Colour black = { 0, 0, 0 };
	for (int y = 0; y < 16; ++y) {
		for (int x = 14; x < 16; ++x) {
			if (!sameAlbedo(state.pixels[std::size_t(y) * 16 + std::size_t(x)].albedo, black)) {
				return false;
			}
		}
	}
	return true;
}

void checkAlbedo() {
	const Lens first         = temporalLens({ 0, 0, 0 }, 16, 16);
	const Lens second        = temporalLens({ 0.1, 0, 0 }, 16, 16);
	const FrameRecord record = albedoRecord();

	const FilmPlan film = {
		.factor = 2,
		.phase  = 3,
	};

	const TestShot opening = {
		.lens      = first,
		.before    = first,
		.film      = film,
		.moved     = false,
		.restarted = true,
		.record    = record,
		.surfaces  = noSurfaces(),
	};

	const Temporal start = shotTemporal(emptyTemporal(), opening);

	const TestShot step = {
		.lens      = second,
		.before    = first,
		.film      = film,
		.moved     = true,
		.restarted = false,
		.record    = record,
		.surfaces  = noSurfaces(),
	};

	const Temporal next = shotTemporal(start.state, step);
	check(aimedAlbedos(start.state, record, film) && aimedAlbedos(next.state, record, film), "temporal: a pixel a sample aimed into holds that sample's albedo");
	check(skyAlbedos(start.state) && skyAlbedos(next.state), "temporal: a pixel showing the sky holds no albedo");
}

int shownColumn(const FilmPlan& film, int x) {
	return x * film.factor + (film.factor > 1 ? film.phase % film.factor : 0);
}

int shownRow(const FilmPlan& film, int y) {
	return y * film.factor + (film.factor > 1 ? film.phase / film.factor : 0);
}

std::vector<Surface> sceneSurfaces(const TestScene& scene) {
	std::vector<Surface> surfaces;
	for (int y = 0; y < scene.lens.height / scene.film.factor; ++y) {
		for (int x = 0; x < scene.lens.width / scene.film.factor; ++x) {
			surfaces.push_back(scene.surface(shownColumn(scene.film, x), shownRow(scene.film, y)));
		}
	}
	return surfaces;
}

FrameRecord sceneRecord(const TestScene& scene) {
	const int factor   = scene.film.factor;
	const int width    = scene.lens.width / factor;
	FrameRecord record = blankRecord(width, scene.lens.height / factor);
	for (int y = 0; y < record.height; ++y) {
		for (int x = 0; x < width; ++x) {
			const std::size_t at   = std::size_t(y) * std::size_t(width) + std::size_t(x);
			const int shownX       = shownColumn(scene.film, x);
			const int shownY       = shownRow(scene.film, y);
			const Surface surface  = scene.surface(shownX, shownY);
			record.surfaces[at]    = surface;
			record.parts[at].total = scene.lighting(shownX, shownY) * surface.albedo;
		}
	}
	return record;
}

double drawnError(const Temporal& temporal, const TestScene& scene, int margin) {
	const int width  = scene.lens.width / scene.film.factor;
	const int height = scene.lens.height / scene.film.factor;
	if (temporal.image.width != width || temporal.image.height != height) {
		return std::numeric_limits<double>::infinity();
	}
	double largest = 0;
	for (int y = margin; y < height - margin; ++y) {
		for (int x = margin; x < width - margin; ++x) {
			const std::size_t at = std::size_t(y) * std::size_t(width) + std::size_t(x);
			const int shownX     = shownColumn(scene.film, x);
			const int shownY     = shownRow(scene.film, y);
			const Colour truth   = scene.lighting(shownX, shownY) * scene.surface(shownX, shownY).albedo;
			largest              = std::max(largest, std::abs(double(temporal.image.rgb[at * 3]) - truth.red));
			largest              = std::max(largest, std::abs(double(temporal.image.rgb[at * 3 + 1]) - truth.green));
			largest              = std::max(largest, std::abs(double(temporal.image.rgb[at * 3 + 2]) - truth.blue));
		}
	}
	return largest;
}

struct SceneShot {
	const TestScene& scene;
	const FrameRecord& record;
	bool restarted;
};

Temporal sceneTemporal(const TemporalState& before, const SceneShot& shot) {
	const std::vector<Surface> surfaces = sceneSurfaces(shot.scene);

	const TestShot taken = {
		.lens      = shot.scene.lens,
		.before    = shot.scene.lens,
		.film      = shot.scene.film,
		.moved     = false,
		.restarted = shot.restarted,
		.record    = shot.record,
		.surfaces  = surfaces,
	};

	return shotTemporal(before, taken);
}

Temporal openedTemporal(const TestScene& scene, const FrameRecord& record) {
	const SceneShot shot = {
		.scene     = scene,
		.record    = record,
		.restarted = true,
	};

	return sceneTemporal(emptyTemporal(), shot);
}

Surface depthSurface(const Lens& lens, int x, int y, double depth) {
	return testSurface(depth * math::length(testHeading(lens, x, y)), 7);
}

FilmPlan quarterFilm() {
	return FilmPlan {
		.factor = 4,
		.phase  = 0,
	};
}

FilmPlan halfFilm(int phase) {
	return FilmPlan {
		.factor = 2,
		.phase  = phase,
	};
}

void checkSurfacedChecker() {
	const Lens lens = temporalLens({ 0, 0, 0 }, 32, 32);

	const TestScene scene = {
		.lens = lens,
		.film = quarterFilm(),

		.surface = [&](int x, int y) {
			Surface surface     = depthSurface(lens, x, y, planeDistance);
			const double albedo = (x / 4 + y / 4) % 2 == 0 ? 0.2 : 0.8;
			surface.albedo      = { albedo, albedo, albedo };
			return surface;
		},

		.lighting = [](int, int) {
			return Colour { 0.5, 0.4, 0.3 };
		},
	};

	const Temporal temporal = openedTemporal(scene, sceneRecord(scene));
	check(temporal.state.demodulated && temporal.state.width == 8 && drawnError(temporal, scene, 0) < 1e-6, "temporal: a G-buffer's checkered albedo shows at the drawn size from one frame at factor 4");
}

FrameRecord crossedRecord(const TestScene& scene, int edge) {
	FrameRecord record = sceneRecord(scene);
	const int column   = edge / scene.film.factor;
	for (int y = 0; y < record.height; ++y) {
		const std::size_t at   = std::size_t(y) * std::size_t(record.width) + std::size_t(column);
		const Surface surface  = scene.surface(edge - 1, shownRow(scene.film, y));
		record.surfaces[at]    = surface;
		record.parts[at].total = scene.lighting(edge - 1, shownRow(scene.film, y)) * surface.albedo;
	}
	return record;
}

void checkSurfacedEdge() {
	const Lens lens = temporalLens({ 0, 0, 0 }, 32, 32);

	const TestScene scene = {
		.lens = lens,
		.film = quarterFilm(),

		.surface = [&](int x, int y) {
			Surface surface = depthSurface(lens, x, y, x < 16 ? 5 : planeDistance);
			surface.leaf    = x < 16 ? 5 : 7;
			return surface;
		},

		.lighting = [](int x, int) {
			return x < 16 ? Colour { 1, 1, 1 } : Colour { 0.1, 0.1, 0.1 };
		},
	};

	const Temporal temporal = openedTemporal(scene, crossedRecord(scene, 16));
	check(drawnError(temporal, scene, 0) < 1e-6, "temporal: a pixel whose sample crossed a depth edge of the G-buffer fills from its own side of it");
}

bool heldEverywhere(const TemporalState& state, int margin) {
	for (int y = margin; y < state.height - margin; ++y) {
		for (int x = margin; x < state.width - margin; ++x) {
			if (!(state.pixels[std::size_t(y) * std::size_t(state.width) + std::size_t(x)].length >= 1)) {
				return false;
			}
		}
	}
	return true;
}

void checkSurfacedReprojection() {
	const Lens first        = temporalLens({ 0, 0, 0 }, 48, 32);
	const double pixelWidth = 2 * first.reach * planeDistance / 32;
	const Lens second       = temporalLens({ 1.25 * pixelWidth, 0, 0 }, 48, 32);

	const TestScene opening = {
		.lens = first,
		.film = halfFilm(0),

		.surface = [&](int x, int y) {
			return depthSurface(first, x, y, planeDistance);
		},

		.lighting = [&](int x, int y) {
			return planeColour(first, x, y);
		},
	};

	const TestScene moved = {
		.lens = second,
		.film = halfFilm(3),

		.surface = [&](int x, int y) {
			return depthSurface(second, x, y, planeDistance);
		},

		.lighting = [&](int x, int y) {
			return planeColour(second, x, y);
		},
	};

	FrameRecord record = sceneRecord(moved);
	for (Surface& surface : record.surfaces) {
		surface.distance *= 2;
	}
	const std::vector<Surface> surfaces = sceneSurfaces(moved);

	const TestShot step = {
		.lens      = second,
		.before    = first,
		.film      = moved.film,
		.moved     = true,
		.restarted = false,
		.record    = record,
		.surfaces  = surfaces,
	};

	const Temporal start = openedTemporal(opening, sceneRecord(opening));
	const Temporal next  = shotTemporal(start.state, step);
	check(heldEverywhere(next.state, 3) && drawnError(next, moved, 3) < 1e-5, "temporal: a sideways step reprojects through the G-buffer's surface, not the samples', from the frame before's jitter to this one's");
}

void checkJitteredStill() {
	const Lens lens     = temporalLens({ 0, 0, 0 }, 48, 32);
	TemporalState state = emptyTemporal();
	double largest      = 0;
	for (std::uint32_t frame = 0; frame < 8; ++frame) {
		const TestScene scene = {
			.lens = lens,
			.film = bayerFilm(2, frame),

			.surface = [&](int x, int y) {
				Surface surface = depthSurface(lens, x, y, planeDistance);
				surface.albedo  = { 0.2 + 0.01 * x, 0.3 + 0.01 * y, 0.5 };
				return surface;
			},

			.lighting = [](int, int) {
				return Colour { 0.6, 0.5, 0.4 };
			},
		};

		const FrameRecord record = sceneRecord(scene);

		const SceneShot shot = {
			.scene     = scene,
			.record    = record,
			.restarted = frame == 0,
		};

		const Temporal temporal = sceneTemporal(state, shot);
		state                   = temporal.state;
		largest                 = std::max(largest, drawnError(temporal, scene, 0));
	}
	check(state.width == 24 && everyLength(state, 8), "temporal: at factor 2 a still camera's rotating phase is not a move, every drawn pixel's history grows");
	check(largest < 1e-6, "temporal: each frame shows the gradient albedo of its own jittered G-buffer exactly");
}

void checkReconstruction() {
	checkStillConvergence();
	checkPhasesFill();
	checkReprojection();
	checkAlbedo();
	checkSurfacedChecker();
	checkSurfacedEdge();
	checkSurfacedReprojection();
	checkJitteredStill();
}

}
