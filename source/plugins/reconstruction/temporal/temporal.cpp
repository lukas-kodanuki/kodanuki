#include "plugins/reconstruction/temporal/temporal.h"

#include "sdf/program.h"
#include "shared/parallel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace kodanuki {

constexpr float movingCap     = 32;
constexpr float stillCap      = 256;
constexpr float youngLength   = 8;
constexpr double movingSpread = 1.25;
constexpr double stillSpread  = 6;
constexpr double depthShare   = 0.03;
constexpr double spreadShare  = 0.25;
constexpr double normalCosine = 0.9;
constexpr double albedoFloor  = 1e-3;

struct TemporalNeighbourhood {
	Colour mean;
	Colour deviation;
	std::size_t closest;
	double spread;
};

struct TemporalCamera {
	math::Vector eye;
	math::Vector forward;
	math::Vector corner;
	math::Vector across;
	math::Vector down;
	math::Vector acrossInverse;
	math::Vector downInverse;
	double width;
	double height;
};

struct TemporalTask {
	const TemporalState& before;
	const ShownFrame& frame;
	bool kept;
	TemporalCamera camera;
	TemporalCamera previous;
	FilmCentre centre;
	FilmCentre previousCentre;
	bool demodulated;
};

struct TemporalSpot {
	int x;
	int y;
	std::size_t nearest;
	bool aimed;
	TemporalNeighbourhood near;
};

struct TemporalSeen {
	float distance;
	math::Vector normal;
	std::uint32_t leaf;
	Colour albedo;
};

struct TemporalHistory {
	bool found;
	Colour colour;
	double moment;
	float length;
	TemporalSeen seen;
};

struct TemporalPlane {
	std::uint32_t leaf;
	math::Vector point;
	math::Vector normal;
	double reach;
};

struct TemporalExpected {
	double distance;
	math::Vector normal;
	std::uint32_t leaf;
	double tolerance;
	bool planar;
	TemporalPlane plane;
};

struct TemporalCell {
	int x;
	int y;
};

struct TemporalPlace {
	bool inside;
	double x;
	double y;
	double distance;
};

struct TemporalTaps {
	int low;
	int high;
	double t;
};

struct TemporalCorners {
	std::size_t topLeft;
	std::size_t topRight;
	std::size_t bottomLeft;
	std::size_t bottomRight;
	double across;
	double down;
};

struct TemporalShare {
	Colour albedo;
	double weight;
};

struct TemporalFill {
	Colour sum;
	double weight;
};

struct TemporalFillTap {
	std::size_t sample;
	double weight;
};

struct TemporalBlend {
	Colour colour;
	double moment;
	float length;
};

struct TemporalSum {
	Colour sum;
	Colour squares;
	std::size_t closest;
	double nearestDistance;
	double farthest;
};

struct TemporalLine {
	int y;
	std::vector<TemporalNeighbourhood> near;
};

struct TemporalRows {
	int count;
	int threads;
	std::function<void(int)> row;
};

bool demodulatedFrame(const ShownFrame& frame) {
	return !frame.surfaces.empty() && frame.surfaces.size() == std::size_t(frame.record.width) * std::size_t(frame.record.height);
}

TemporalGrid temporalGrid(const ShownFrame& frame) {
	const bool demodulated = demodulatedFrame(frame);
	return TemporalGrid {
		.width       = demodulated ? frame.record.width : frame.width,
		.height      = demodulated ? frame.record.height : frame.height,
		.demodulated = demodulated,
	};
}

int gridFactor(const ShownFrame& frame) {
	return demodulatedFrame(frame) ? 1 : frame.film.factor;
}

int phaseColumn(const ShownFrame& frame) {
	return gridFactor(frame) > 1 ? frame.film.phase % frame.film.factor : 0;
}

int phaseRow(const ShownFrame& frame) {
	return gridFactor(frame) > 1 ? frame.film.phase / frame.film.factor : 0;
}

FilmCentre temporalCentre(const TemporalState& state) {
	return filmCentre(state.demodulated ? state.film : stillFilm());
}

int nearestDrawnAlong(int shown, int offset, int factor, int count) {
	const int drawn = (shown - offset + factor / 2 + factor) / factor - 1;
	return std::clamp(drawn, 0, count - 1);
}

std::size_t nearestDrawn(const ShownFrame& frame, int x, int y) {
	const int factor = gridFactor(frame);
	const int column = nearestDrawnAlong(x, phaseColumn(frame), factor, frame.record.width);
	const int row    = nearestDrawnAlong(y, phaseRow(frame), factor, frame.record.height);
	return std::size_t(row) * std::size_t(frame.record.width) + std::size_t(column);
}

bool aimedInto(const ShownFrame& frame, int x, int y) {
	const int factor = gridFactor(frame);
	return x % factor == phaseColumn(frame) && y % factor == phaseRow(frame);
}

double channelDivisor(double albedo) {
	return albedo < albedoFloor ? 1 : albedo;
}

Colour lightingDivisor(const Colour& albedo) {
	return { channelDivisor(albedo.red), channelDivisor(albedo.green), channelDivisor(albedo.blue) };
}

Colour dividedLight(const Colour& light, const Colour& divisor) {
	return { light.red / divisor.red, light.green / divisor.green, light.blue / divisor.blue };
}

Colour shownLight(const TemporalPixel& pixel, bool demodulated) {
	return demodulated ? pixel.colour * lightingDivisor(pixel.albedo) : pixel.colour;
}

Colour sampleLight(const ShownFrame& frame, std::size_t sample) {
	const Colour& light = frame.record.parts[sample].total;
	if (!demodulatedFrame(frame)) {
		return light;
	}
	return dividedLight(light, lightingDivisor(frame.record.surfaces[sample].albedo));
}

const Surface& shownSurfaceAt(const ShownFrame& frame, int x, int y) {
	return frame.surfaces[std::size_t(y) * std::size_t(frame.record.width) + std::size_t(x)];
}

bool skySurface(const Surface& surface) {
	return surface.leaf == noLeaf || !std::isfinite(surface.distance);
}

std::uint32_t shownLeaf(const Surface& surface) {
	return skySurface(surface) ? noLeaf : surface.leaf;
}

bool aimedSurface(const ShownFrame& frame, int x, int y) {
	if (!aimedInto(frame, x, y)) {
		return false;
	}
	if (!demodulatedFrame(frame)) {
		return true;
	}
	return shownLeaf(frame.record.surfaces[nearestDrawn(frame, x, y)]) == shownLeaf(shownSurfaceAt(frame, x, y));
}

TemporalSeen seenOfSurface(const Surface& surface) {
	return TemporalSeen {
		.distance = surface.distance,
		.normal   = surface.normal,
		.leaf     = surface.leaf,
		.albedo   = surface.albedo,
	};
}

TemporalSeen seenOfPixel(const TemporalPixel& pixel) {
	return TemporalSeen {
		.distance = pixel.distance,
		.normal   = pixel.normal,
		.leaf     = pixel.leaf,
		.albedo   = pixel.albedo,
	};
}

TemporalPixel pixelFrom(const TemporalBlend& blend, const TemporalSeen& seen) {
	return TemporalPixel {
		.colour   = blend.colour,
		.moment   = blend.moment,
		.length   = blend.length,
		.distance = seen.distance,
		.normal   = seen.normal,
		.leaf     = seen.leaf,
		.albedo   = seen.albedo,
	};
}

TemporalHistory noHistory() {
	return TemporalHistory {
		.found  = false,
		.colour = { 0, 0, 0 },
		.moment = 0,
		.length = 0,

		.seen = {
			.distance = 0,
			.normal   = { 0, 0, 0 },
			.leaf     = noLeaf,
			.albedo   = { 0, 0, 0 },
		},
	};
}

Lens temporalGridLens(const Lens& lens, const TemporalGrid& grid) {
	Lens sized   = lens;
	sized.width  = grid.width;
	sized.height = grid.height;
	return sized;
}

TemporalCamera temporalCamera(const Lens& lens) {
	const double sideways     = lens.reach * lens.aspect;
	const math::Vector across = lens.right * (2 * sideways / double(lens.width));
	const math::Vector down   = lens.up * (-2 * lens.reach / double(lens.height));
	return TemporalCamera {
		.eye           = lens.eye,
		.forward       = lens.forward,
		.corner        = lens.forward - lens.right * sideways + lens.up * lens.reach,
		.across        = across,
		.down          = down,
		.acrossInverse = across * (1 / math::dot(across, across)),
		.downInverse   = down * (1 / math::dot(down, down)),
		.width         = double(lens.width),
		.height        = double(lens.height),
	};
}

math::Vector filmHeading(const TemporalCamera& camera, double x, double y) {
	return camera.corner + camera.across * x + camera.down * y;
}

math::Vector spotHeading(const TemporalTask& task, const TemporalSpot& spot) {
	return filmHeading(task.camera, double(spot.x) + task.centre.x, double(spot.y) + task.centre.y);
}

math::Vector sampleHeading(const TemporalTask& task, std::size_t sample) {
	const int factor        = gridFactor(task.frame);
	const std::size_t width = std::size_t(task.frame.record.width);
	const double x          = double(int(sample % width) * factor + phaseColumn(task.frame)) + task.centre.x;
	const double y          = double(int(sample / width) * factor + phaseRow(task.frame)) + task.centre.y;
	return filmHeading(task.camera, x, y);
}

math::Vector seenPoint(const TemporalTask& task, const math::Vector& heading, std::size_t sample) {
	const double depth = double(task.frame.record.surfaces[sample].distance) / math::length(sampleHeading(task, sample));
	return task.camera.eye + heading * depth;
}

math::Vector surfacePointAlong(const math::Vector& eye, const math::Vector& heading, double distance) {
	return eye + heading * (distance / math::length(heading));
}

TemporalPlane skyPlane() {
	return TemporalPlane {
		.leaf   = noLeaf,
		.point  = { 0, 0, 0 },
		.normal = { 0, 0, 0 },
		.reach  = 0,
	};
}

TemporalPlane planeThrough(const Surface& surface, const math::Vector& eye, const math::Vector& heading) {
	if (skySurface(surface)) {
		return skyPlane();
	}
	return TemporalPlane {
		.leaf   = surface.leaf,
		.point  = surfacePointAlong(eye, heading, double(surface.distance)),
		.normal = surface.normal,
		.reach  = depthShare * double(surface.distance),
	};
}

TemporalPlane shownPlane(const TemporalTask& task, const TemporalSpot& spot) {
	return planeThrough(shownSurfaceAt(task.frame, spot.x, spot.y), task.camera.eye, spotHeading(task, spot));
}

TemporalPlane samplePlane(const TemporalTask& task, std::size_t sample) {
	return planeThrough(task.frame.record.surfaces[sample], task.camera.eye, sampleHeading(task, sample));
}

TemporalPlane lastPlane(const TemporalTask& task, const TemporalCell& cell) {
	const TemporalPixel& last  = task.before.pixels[std::size_t(cell.y) * std::size_t(task.before.width) + std::size_t(cell.x)];
	const math::Vector heading = filmHeading(task.previous, double(cell.x) + task.previousCentre.x, double(cell.y) + task.previousCentre.y);
	const bool sky             = last.leaf == noLeaf || !std::isfinite(last.distance);
	if (sky) {
		return skyPlane();
	}
	return TemporalPlane {
		.leaf   = last.leaf,
		.point  = surfacePointAlong(task.previous.eye, heading, double(last.distance)),
		.normal = last.normal,
		.reach  = 0,
	};
}

bool sameLeafOf(std::uint32_t a, std::uint32_t b, bool recounted) {
	return recounted ? (a == noLeaf) == (b == noLeaf) : a == b;
}

bool onPlane(const TemporalPlane& plane, const TemporalPlane& other, bool recounted) {
	if (!sameLeafOf(other.leaf, plane.leaf, recounted)) {
		return false;
	}
	if (plane.leaf == noLeaf) {
		return true;
	}
	if (math::dot(other.normal, plane.normal) < normalCosine) {
		return false;
	}
	return std::abs(math::dot(other.point - plane.point, plane.normal)) <= plane.reach;
}

TemporalPlace outsidePlace() {
	return TemporalPlace {
		.inside   = false,
		.x        = 0,
		.y        = 0,
		.distance = 0,
	};
}

TemporalPlace placeBefore(const TemporalCamera& before, const math::Vector& along) {
	const double ahead = math::dot(along, before.forward);
	if (!(ahead > 0)) {
		return outsidePlace();
	}
	const double scale = 1 / ahead;
	const double x     = math::dot(along, before.acrossInverse) * scale + 0.5 * before.width;
	const double y     = math::dot(along, before.downInverse) * scale + 0.5 * before.height;
	if (!(x >= 0 && x < before.width && y >= 0 && y < before.height)) {
		return outsidePlace();
	}
	return TemporalPlace {
		.inside   = true,
		.x        = x,
		.y        = y,
		.distance = math::length(along),
	};
}

bool sameSeen(const TemporalPixel& last, const TemporalExpected& expected, bool recounted) {
	if (!(last.length > 0) || !sameLeafOf(last.leaf, expected.leaf, recounted)) {
		return false;
	}
	if (expected.leaf == noLeaf) {
		return true;
	}
	if (math::dot(last.normal, expected.normal) < normalCosine) {
		return false;
	}
	return std::abs(double(last.distance) - expected.distance) <= expected.tolerance;
}

TemporalTaps tapsAt(double at, double centre) {
	const double place = at - centre;
	const int low      = int(std::floor(place));
	return TemporalTaps {
		.low  = low,
		.high = low + 1,
		.t    = place - double(low),
	};
}

double tapWeight(const TemporalTaps& taps, int side) {
	return side == 0 ? 1 - taps.t : taps.t;
}

TemporalSeen averagedSeen(const TemporalSeen& sum, double weight) {
	return TemporalSeen {
		.distance = sum.distance,
		.normal   = sum.normal,
		.leaf     = sum.leaf,
		.albedo   = sum.albedo * (1 / weight),
	};
}

TemporalHistory summedHistory(const TemporalHistory& sum, double weight) {
	if (!(weight > 0)) {
		return noHistory();
	}
	return TemporalHistory {
		.found  = true,
		.colour = sum.colour * (1 / weight),
		.moment = sum.moment / weight,
		.length = float(double(sum.length) / weight),
		.seen   = averagedSeen(sum.seen, weight),
	};
}

bool heldSeen(const TemporalTask& task, const TemporalCell& cell, const TemporalExpected& expected) {
	const TemporalPixel& last = task.before.pixels[std::size_t(cell.y) * std::size_t(task.before.width) + std::size_t(cell.x)];
	if (!expected.planar) {
		return sameSeen(last, expected, task.frame.recounted);
	}
	return last.length > 0 && onPlane(expected.plane, lastPlane(task, cell), task.frame.recounted);
}

TemporalHistory fetchedHistory(const TemporalTask& task, const TemporalPlace& place, const TemporalExpected& expected) {
	const TemporalState& before = task.before;
	const TemporalTaps across   = tapsAt(place.x, task.previousCentre.x);
	const TemporalTaps down     = tapsAt(place.y, task.previousCentre.y);
	TemporalHistory sum         = noHistory();
	double weight               = 0;
	for (int j = 0; j < 2; ++j) {
		for (int i = 0; i < 2; ++i) {
			const int x     = across.low + i;
			const int y     = down.low + j;
			const double w  = tapWeight(across, i) * tapWeight(down, j);
			const bool held = w > 0 && x >= 0 && x < before.width && y >= 0 && y < before.height;
			if (!held) {
				continue;
			}

			const TemporalCell cell = {
				.x = x,
				.y = y,
			};

			if (!heldSeen(task, cell, expected)) {
				continue;
			}
			const TemporalPixel& last = before.pixels[std::size_t(y) * std::size_t(before.width) + std::size_t(x)];
			sum.colour                = sum.colour + last.colour * w;
			sum.seen.albedo           = sum.seen.albedo + last.albedo * w;
			sum.moment += last.moment * w;
			sum.length += float(double(last.length) * w);
			weight += w;
		}
	}
	return summedHistory(sum, weight);
}

TemporalExpected expectedOf(const Surface& surface, const TemporalPlace& place, double spread) {
	const double distance = surface.leaf == noLeaf ? double(surface.distance) : place.distance;
	return TemporalExpected {
		.distance  = distance,
		.normal    = surface.normal,
		.leaf      = surface.leaf,
		.tolerance = depthShare * distance + std::min(spread, spreadShare * distance),
		.planar    = false,
		.plane     = skyPlane(),
	};
}

TemporalHistory withSeen(const TemporalHistory& history, const TemporalSeen& seen) {
	TemporalHistory seenHistory = history;
	seenHistory.seen            = seen;
	return seenHistory;
}

TemporalHistory reprojectedHistory(const TemporalTask& task, const TemporalSpot& spot, std::size_t sample) {
	const Surface& surface     = task.frame.record.surfaces[sample];
	const math::Vector heading = spotHeading(task, spot);
	const bool sky             = surface.leaf == noLeaf || !std::isfinite(surface.distance);
	const math::Vector point   = sky ? heading : seenPoint(task, heading, sample);
	const math::Vector along   = sky ? heading : point - task.previous.eye;
	const TemporalPlace place  = placeBefore(task.previous, along);
	if (!place.inside) {
		return noHistory();
	}
	const TemporalHistory history = fetchedHistory(task, place, expectedOf(surface, place, spot.near.spread));

	const TemporalSeen seen = {
		.distance = sky ? surface.distance : float(math::length(point - task.camera.eye)),
		.normal   = surface.normal,
		.leaf     = sky ? noLeaf : surface.leaf,
		.albedo   = sky ? Colour { 0, 0, 0 } : history.seen.albedo,
	};

	return withSeen(history, seen);
}

TemporalHistory stillHistory(const TemporalTask& task, const TemporalSpot& spot) {
	const TemporalPixel& last = task.before.pixels[std::size_t(spot.y) * std::size_t(task.before.width) + std::size_t(spot.x)];
	if (!(last.length > 0)) {
		return noHistory();
	}
	return TemporalHistory {
		.found  = true,
		.colour = last.colour,
		.moment = last.moment,
		.length = last.length,
		.seen   = seenOfPixel(last),
	};
}

TemporalHistory surfacedHistory(const TemporalTask& task, const TemporalSpot& spot) {
	const TemporalPlane plane  = shownPlane(task, spot);
	const math::Vector heading = spotHeading(task, spot);
	const math::Vector along   = plane.leaf == noLeaf ? heading : plane.point - task.previous.eye;
	const TemporalPlace place  = placeBefore(task.previous, along);
	if (!place.inside) {
		return noHistory();
	}

	const TemporalExpected expected = {
		.distance  = place.distance,
		.normal    = plane.normal,
		.leaf      = plane.leaf,
		.tolerance = plane.reach,
		.planar    = true,
		.plane     = plane,
	};

	return fetchedHistory(task, place, expected);
}

TemporalHistory historyFor(const TemporalTask& task, const TemporalSpot& spot) {
	if (!task.kept) {
		return noHistory();
	}
	if (!task.frame.moved) {
		return stillHistory(task, spot);
	}
	if (task.demodulated) {
		return surfacedHistory(task, spot);
	}
	const TemporalHistory nearest = reprojectedHistory(task, spot, spot.nearest);
	const std::size_t closest     = spot.near.closest;
	if (nearest.found || closest == spot.nearest) {
		return nearest;
	}
	return reprojectedHistory(task, spot, closest);
}

TemporalTaps tapsAlong(int shown, int offset, const ShownFrame& frame, int count) {
	const double place = double(shown - offset) / double(gridFactor(frame));
	const int low      = int(std::floor(place));
	if (low < 0) {
		return TemporalTaps {
			.low  = 0,
			.high = 0,
			.t    = 0,
		};
	}
	if (low >= count - 1) {
		return TemporalTaps {
			.low  = count - 1,
			.high = count - 1,
			.t    = 0,
		};
	}
	return TemporalTaps {
		.low  = low,
		.high = low + 1,
		.t    = place - double(low),
	};
}

TemporalCorners cornersOf(const ShownFrame& frame, int x, int y) {
	const TemporalTaps across = tapsAlong(x, phaseColumn(frame), frame, frame.record.width);
	const TemporalTaps down   = tapsAlong(y, phaseRow(frame), frame, frame.record.height);
	const std::size_t width   = std::size_t(frame.record.width);
	const std::size_t low     = std::size_t(down.low) * width;
	const std::size_t high    = std::size_t(down.high) * width;
	return TemporalCorners {
		.topLeft     = low + std::size_t(across.low),
		.topRight    = low + std::size_t(across.high),
		.bottomLeft  = high + std::size_t(across.low),
		.bottomRight = high + std::size_t(across.high),
		.across      = across.t,
		.down        = down.t,
	};
}

TemporalShare surfaceShare(const ShownFrame& frame, std::size_t sample, double weight) {
	const Surface& surface = frame.record.surfaces[sample];
	if (surface.leaf == noLeaf) {
		return TemporalShare {
			.albedo = { 0, 0, 0 },
			.weight = 0,
		};
	}
	return TemporalShare {
		.albedo = surface.albedo * weight,
		.weight = weight,
	};
}

TemporalShare joinedShares(const TemporalShare& a, const TemporalShare& b) {
	return TemporalShare {
		.albedo = a.albedo + b.albedo,
		.weight = a.weight + b.weight,
	};
}

Colour surfaceAlbedoOf(const ShownFrame& frame, const TemporalCorners& corners) {
	const TemporalShare top    = joinedShares(surfaceShare(frame, corners.topLeft, (1 - corners.across) * (1 - corners.down)), surfaceShare(frame, corners.topRight, corners.across * (1 - corners.down)));
	const TemporalShare bottom = joinedShares(surfaceShare(frame, corners.bottomLeft, (1 - corners.across) * corners.down), surfaceShare(frame, corners.bottomRight, corners.across * corners.down));
	const TemporalShare all    = joinedShares(top, bottom);
	return all.weight > 0 ? all.albedo * (1 / all.weight) : Colour { 0, 0, 0 };
}

Colour filledColour(const ShownFrame& frame, int x, int y) {
	const TemporalCorners corners = cornersOf(frame, x, y);
	const Colour top              = sampleLight(frame, corners.topLeft) * (1 - corners.across) + sampleLight(frame, corners.topRight) * corners.across;
	const Colour bottom           = sampleLight(frame, corners.bottomLeft) * (1 - corners.across) + sampleLight(frame, corners.bottomRight) * corners.across;
	return top * (1 - corners.down) + bottom * corners.down;
}

std::size_t clampedDrawn(const FrameRecord& record, int x, int y) {
	const int column = std::clamp(x, 0, record.width - 1);
	const int row    = std::clamp(y, 0, record.height - 1);
	return std::size_t(row) * std::size_t(record.width) + std::size_t(column);
}

TemporalFillTap fillTap(std::size_t sample, double weight) {
	return TemporalFillTap {
		.sample = sample,
		.weight = weight,
	};
}

TemporalFill fillShare(const TemporalTask& task, const TemporalPlane& plane, const TemporalFillTap& tap) {
	if (!(tap.weight > 0) || !onPlane(plane, samplePlane(task, tap.sample), false)) {
		return TemporalFill {
			.sum    = { 0, 0, 0 },
			.weight = 0,
		};
	}
	return TemporalFill {
		.sum    = sampleLight(task.frame, tap.sample) * tap.weight,
		.weight = tap.weight,
	};
}

TemporalFill joinedFills(const TemporalFill& a, const TemporalFill& b) {
	return TemporalFill {
		.sum    = a.sum + b.sum,
		.weight = a.weight + b.weight,
	};
}

TemporalFill cornerFill(const TemporalTask& task, const TemporalPlane& plane, const TemporalCorners& corners) {
	const TemporalFill top    = joinedFills(fillShare(task, plane, fillTap(corners.topLeft, (1 - corners.across) * (1 - corners.down))), fillShare(task, plane, fillTap(corners.topRight, corners.across * (1 - corners.down))));
	const TemporalFill bottom = joinedFills(fillShare(task, plane, fillTap(corners.bottomLeft, (1 - corners.across) * corners.down)), fillShare(task, plane, fillTap(corners.bottomRight, corners.across * corners.down)));
	return joinedFills(top, bottom);
}

TemporalFill aroundFill(const TemporalTask& task, const TemporalPlane& plane, std::size_t nearest) {
	const int x = int(nearest % std::size_t(task.frame.record.width));
	const int y = int(nearest / std::size_t(task.frame.record.width));

	TemporalFill fill = {
		.sum    = { 0, 0, 0 },
		.weight = 0,
	};

	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			fill = joinedFills(fill, fillShare(task, plane, fillTap(clampedDrawn(task.frame.record, x + dx, y + dy), 1)));
		}
	}
	return fill;
}

Colour jointFilled(const TemporalTask& task, const TemporalSpot& spot) {
	const TemporalPlane plane = shownPlane(task, spot);
	const TemporalFill corner = cornerFill(task, plane, cornersOf(task.frame, spot.x, spot.y));
	if (corner.weight > 0) {
		return corner.sum * (1 / corner.weight);
	}
	const TemporalFill around = aroundFill(task, plane, spot.nearest);
	if (around.weight > 0) {
		return around.sum * (1 / around.weight);
	}
	return filledColour(task.frame, spot.x, spot.y);
}

double clampedChannel(double value, double mean, double reach) {
	return std::clamp(value, mean - reach, mean + reach);
}

Colour clampedColour(const Colour& colour, const TemporalNeighbourhood& near, double spread) {
	return Colour {
		clampedChannel(colour.red, near.mean.red, spread * near.deviation.red),
		clampedChannel(colour.green, near.mean.green, spread * near.deviation.green),
		clampedChannel(colour.blue, near.mean.blue, spread * near.deviation.blue),
	};
}

TemporalSeen freshSeen(const ShownFrame& frame, const TemporalSpot& spot) {
	const TemporalSeen seen = seenOfSurface(frame.record.surfaces[spot.nearest]);
	return TemporalSeen {
		.distance = seen.distance,
		.normal   = seen.normal,
		.leaf     = seen.leaf,
		.albedo   = spot.aimed ? seen.albedo : surfaceAlbedoOf(frame, cornersOf(frame, spot.x, spot.y)),
	};
}

Colour freshColour(const TemporalTask& task, const TemporalSpot& spot) {
	if (spot.aimed) {
		return sampleLight(task.frame, spot.nearest);
	}
	return task.demodulated ? jointFilled(task, spot) : filledColour(task.frame, spot.x, spot.y);
}

TemporalBlend freshBlend(const TemporalTask& task, const TemporalSpot& spot) {
	const Colour colour    = freshColour(task, spot);
	const double luminance = luminanceOf(colour);
	return TemporalBlend {
		.colour = colour,
		.moment = luminance * luminance,
		.length = spot.aimed ? 1.0f : 0.0f,
	};
}

TemporalPixel freshPixel(const TemporalTask& task, const TemporalSpot& spot) {
	return pixelFrom(freshBlend(task, spot), freshSeen(task.frame, spot));
}

Colour heldColour(const TemporalTask& task, const TemporalSpot& spot, const TemporalHistory& history) {
	if (!task.frame.moved && !(history.length < youngLength)) {
		return history.colour;
	}
	return clampedColour(history.colour, spot.near, task.frame.moved ? movingSpread : stillSpread);
}

TemporalSeen blendedSeen(const TemporalSeen& history, const Surface& surface, double alpha) {
	const TemporalSeen seen = seenOfSurface(surface);
	return TemporalSeen {
		.distance = seen.distance,
		.normal   = seen.normal,
		.leaf     = seen.leaf,
		.albedo   = history.albedo * (1 - alpha) + seen.albedo * alpha,
	};
}

float capOf(const TemporalTask& task) {
	return task.frame.moved ? movingCap : stillCap;
}

double historyShare(const TemporalTask& task, const TemporalHistory& history) {
	const float cap = capOf(task);
	return std::max(1.0 / (double(std::min(history.length, cap)) + 1), 1.0 / double(cap));
}

TemporalBlend heldBlend(const TemporalTask& task, const TemporalSpot& spot, const TemporalHistory& history) {
	const float cap    = capOf(task);
	const float length = std::min(history.length, cap);
	const Colour held  = heldColour(task, spot, history);
	if (!spot.aimed) {
		return TemporalBlend {
			.colour = held,
			.moment = history.moment,
			.length = length,
		};
	}
	const Colour sample    = sampleLight(task.frame, spot.nearest);
	const double luminance = luminanceOf(sample);
	const double alpha     = historyShare(task, history);
	return TemporalBlend {
		.colour = held * (1 - alpha) + sample * alpha,
		.moment = history.moment * (1 - alpha) + luminance * luminance * alpha,
		.length = std::min(length + 1, cap),
	};
}

TemporalPixel blendedPixel(const TemporalTask& task, const TemporalSpot& spot, const TemporalHistory& history) {
	const TemporalBlend blend = heldBlend(task, spot, history);
	if (!spot.aimed) {
		return pixelFrom(blend, history.seen);
	}
	return pixelFrom(blend, blendedSeen(history.seen, task.frame.record.surfaces[spot.nearest], historyShare(task, history)));
}

TemporalPixel surfacedPixel(const TemporalTask& task, const TemporalSpot& spot, const TemporalHistory& history) {
	const TemporalBlend blend = history.found ? heldBlend(task, spot, history) : freshBlend(task, spot);
	return pixelFrom(blend, seenOfSurface(shownSurfaceAt(task.frame, spot.x, spot.y)));
}

TemporalSum sampleSum(const ShownFrame& frame, std::size_t sample) {
	const Colour colour   = sampleLight(frame, sample);
	const double distance = double(frame.record.surfaces[sample].distance);
	return TemporalSum {
		.sum             = colour,
		.squares         = colour * colour,
		.closest         = sample,
		.nearestDistance = distance,
		.farthest        = std::isfinite(distance) ? distance : 0,
	};
}

TemporalSum joinedSums(const TemporalSum& a, const TemporalSum& b) {
	const bool closer = b.nearestDistance < a.nearestDistance;
	return TemporalSum {
		.sum             = a.sum + b.sum,
		.squares         = a.squares + b.squares,
		.closest         = closer ? b.closest : a.closest,
		.nearestDistance = closer ? b.nearestDistance : a.nearestDistance,
		.farthest        = std::max(a.farthest, b.farthest),
	};
}

TemporalSum columnSum(const ShownFrame& frame, int x, int y) {
	const TemporalSum middle = joinedSums(sampleSum(frame, clampedDrawn(frame.record, x, y)), sampleSum(frame, clampedDrawn(frame.record, x, y - 1)));
	return joinedSums(middle, sampleSum(frame, clampedDrawn(frame.record, x, y + 1)));
}

double deviationOf(double sum, double squares) {
	const double mean = sum / 9;
	return std::sqrt(std::max(squares / 9 - mean * mean, 0.0));
}

TemporalNeighbourhood neighbourhoodOf(const TemporalSum& sum) {
	const bool hit = std::isfinite(sum.nearestDistance);
	return TemporalNeighbourhood {
		.mean      = sum.sum * (1.0 / 9),
		.deviation = { deviationOf(sum.sum.red, sum.squares.red), deviationOf(sum.sum.green, sum.squares.green), deviationOf(sum.sum.blue, sum.squares.blue) },
		.closest   = sum.closest,
		.spread    = hit ? sum.farthest - sum.nearestDistance : 0,
	};
}

std::vector<TemporalNeighbourhood> rowNeighbourhoods(const ShownFrame& frame, int y) {
	const FrameRecord& record = frame.record;
	std::vector<TemporalSum> columns;
	columns.reserve(std::size_t(record.width));
	for (int x = 0; x < record.width; ++x) {
		columns.push_back(columnSum(frame, x, y));
	}
	std::vector<TemporalNeighbourhood> near;
	near.reserve(std::size_t(record.width));
	for (int x = 0; x < record.width; ++x) {
		const TemporalSum& left  = columns[std::size_t(std::max(x - 1, 0))];
		const TemporalSum& right = columns[std::size_t(std::min(x + 1, record.width - 1))];
		near.push_back(neighbourhoodOf(joinedSums(joinedSums(columns[std::size_t(x)], left), right)));
	}
	return near;
}

TemporalPixel temporalPixel(const TemporalTask& task, const TemporalLine& line, int x) {
	const std::size_t nearest = nearestDrawn(task.frame, x, line.y);

	const TemporalSpot spot = {
		.x       = x,
		.y       = line.y,
		.nearest = nearest,
		.aimed   = aimedSurface(task.frame, x, line.y),
		.near    = line.near[nearest % std::size_t(task.frame.record.width)],
	};

	const TemporalHistory history = historyFor(task, spot);
	if (task.demodulated) {
		return surfacedPixel(task, spot, history);
	}
	if (!history.found) {
		return freshPixel(task, spot);
	}
	return blendedPixel(task, spot, history);
}

void temporalRow(const TemporalTask& task, int y, Temporal& next) {
	const ShownFrame& frame = task.frame;
	const int drawnRow      = nearestDrawnAlong(y, phaseRow(frame), gridFactor(frame), frame.record.height);
	const int width         = next.state.width;

	const TemporalLine line = {
		.y    = y,
		.near = rowNeighbourhoods(frame, drawnRow),
	};

	for (int x = 0; x < width; ++x) {
		const std::size_t at       = std::size_t(y) * std::size_t(width) + std::size_t(x);
		const TemporalPixel pixel  = temporalPixel(task, line, x);
		const Colour shown         = shownLight(pixel, task.demodulated);
		next.state.pixels[at]      = pixel;
		next.image.rgb[at * 3]     = float(shown.red);
		next.image.rgb[at * 3 + 1] = float(shown.green);
		next.image.rgb[at * 3 + 2] = float(shown.blue);
	}
}

void eachTemporalRow(const TemporalRows& rows) {
	inParallel(rows.threads, std::size_t(std::max(rows.count, 0)), [&](std::size_t y) {
		rows.row(int(y));
	});
}

bool keptHistory(const TemporalState& before, const ShownFrame& frame) {
	const TemporalGrid grid = temporalGrid(frame);
	const bool sized        = before.width == grid.width && before.height == grid.height && before.width > 0;
	const bool alike        = before.demodulated == grid.demodulated;
	return !frame.restarted && sized && alike && before.pixels.size() == std::size_t(grid.width) * std::size_t(grid.height);
}

TemporalState emptyTemporal() {
	return TemporalState {
		.width       = 0,
		.height      = 0,
		.demodulated = false,
		.film        = stillFilm(),
		.pixels      = {},
		.spare       = {},
	};
}

std::vector<TemporalPixel> sparePixels(std::vector<TemporalPixel> spare, std::size_t count) {
	spare.resize(count);
	return spare;
}

Temporal temporalFrame(TemporalState before, const ShownFrame& frame) {
	const TemporalGrid grid           = temporalGrid(frame);
	std::vector<TemporalPixel> pixels = sparePixels(std::move(before.spare), std::size_t(grid.width) * std::size_t(grid.height));

	const TemporalTask task = {
		.before         = before,
		.frame          = frame,
		.kept           = keptHistory(before, frame),
		.camera         = temporalCamera(temporalGridLens(frame.lens, grid)),
		.previous       = temporalCamera(temporalGridLens(frame.before, grid)),
		.centre         = filmCentre(grid.demodulated ? frame.film : stillFilm()),
		.previousCentre = temporalCentre(before),
		.demodulated    = grid.demodulated,
	};

	Temporal next = {
		.image = blankImage(grid.width, grid.height),

		.state = {
			.width       = grid.width,
			.height      = grid.height,
			.demodulated = grid.demodulated,
			.film        = frame.film,
			.pixels      = std::move(pixels),
			.spare       = {},
		},
	};

	const auto row = [&](int y) {
		temporalRow(task, y, next);
	};

	const TemporalRows rows = {
		.count   = grid.height,
		.threads = frame.threads,
		.row     = row,
	};

	eachTemporalRow(rows);
	next.state.spare = std::move(before.pixels);
	return next;
}

std::shared_ptr<void> openedTemporalState() {
	return std::make_shared<TemporalState>(emptyTemporal());
}

TemporalState& temporalState(States& states) {
	return *static_cast<TemporalState*>(heldState(states, "reconstruction/temporal", openedTemporalState));
}

}
