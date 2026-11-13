#include "plugins/reconstruction/upscale/upscale.h"

#include "shared/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace kodanuki {

constexpr float upscaleCeiling     = 60000;
constexpr float upscaleFloor       = 1e-4f;
constexpr double upscaleSharp      = 8;
constexpr float upscaleAlpha       = 0.5f;
constexpr float upscaleAlphaSettle = 1;
constexpr float upscaleAlphaFast   = 1;
constexpr float upscaleAlphaLeast  = 1e-5f;
constexpr float upscaleAlphaMoving = 0.15f;
constexpr double upscaleFast       = 8;
constexpr float upscaleClamp       = 1.25f;
constexpr float upscaleClampStill  = 6;
constexpr double upscaleStill      = 1;
constexpr float upscaleStillMargin = 1;
constexpr float upscaleDepthShare  = 0.03f;
constexpr float upscaleSpreadShare = 0.25f;
constexpr int upscaleSettleFrames  = 4;
constexpr float upscaleSky         = std::numeric_limits<float>::infinity();
constexpr float upscaleLeastWeight = 1e-6f;

struct UpscaleTone {
	float luma;
	float orange;
	float green;
};

struct UpscaleSample {
	UpscaleTone tone;
	float depth;
};

struct UpscaleAxis {
	std::array<int, 3> taps;
	std::array<float, 3> sharp;
	std::array<float, 3> fill;
	int toward;
	int box;
};

struct UpscaleLine {
	int factor;
	double jitter;
	int count;
};

struct UpscaleStats {
	UpscaleTone mean;
	UpscaleTone deviation;
	UpscaleTone least;
	UpscaleTone most;
	float closest;
	float farthest;
};

struct UpscaleGrid {
	const std::vector<UpscaleSample>& samples;
	int width;
	int height;
};

struct UpscaleTask {
	const UpscaleState& before;
	const ShownFrame& frame;
	const std::vector<UpscaleSample>& samples;
	const std::vector<UpscaleAxis>& columns;
	const std::vector<UpscaleAxis>& rows;
	const std::vector<UpscaleStats>& boxes;
	int width;
	bool kept;
	bool settling;
};

struct UpscaleSpot {
	int x;
	int y;
	const UpscaleAxis& column;
	const UpscaleAxis& row;
};

struct UpscaleGather {
	UpscaleTone sharp;
	float sharpWeight;
	UpscaleTone fill;
	const UpscaleStats& stats;
};

struct UpscaleNear {
	UpscaleSpot spot;
	UpscaleGather gather;
};

struct UpscaleBox {
	UpscaleTone least;
	UpscaleTone most;
};

struct UpscaleHeld {
	UpscaleTone history;
	float motion;
};

struct UpscaleSeen {
	bool inside;
	double x;
	double y;
	float depth;
};

struct UpscaleTaps {
	int before;
	double middle;
	int after;
	double middleWeight;
	double beforeWeight;
	double afterWeight;
};

struct UpscaleRows {
	int count;
	int threads;
	std::function<void(int)> row;
};

UpscaleTone operator+(const UpscaleTone& a, const UpscaleTone& b) {
	return { a.luma + b.luma, a.orange + b.orange, a.green + b.green };
}

UpscaleTone operator-(const UpscaleTone& a, const UpscaleTone& b) {
	return { a.luma - b.luma, a.orange - b.orange, a.green - b.green };
}

UpscaleTone operator*(const UpscaleTone& a, float s) {
	return { a.luma * s, a.orange * s, a.green * s };
}

UpscaleTone toneLeast(const UpscaleTone& a, const UpscaleTone& b) {
	return { std::min(a.luma, b.luma), std::min(a.orange, b.orange), std::min(a.green, b.green) };
}

UpscaleTone toneMost(const UpscaleTone& a, const UpscaleTone& b) {
	return { std::max(a.luma, b.luma), std::max(a.orange, b.orange), std::max(a.green, b.green) };
}

UpscaleTone toneSquared(const UpscaleTone& a) {
	return { a.luma * a.luma, a.orange * a.orange, a.green * a.green };
}

UpscaleTone toneRoot(const UpscaleTone& a) {
	return { std::sqrt(std::max(a.luma, 0.0f)), std::sqrt(std::max(a.orange, 0.0f)), std::sqrt(std::max(a.green, 0.0f)) };
}

UpscaleTone toneClamped(const UpscaleTone& a, const UpscaleTone& low, const UpscaleTone& high) {
	return toneLeast(toneMost(a, low), high);
}

UpscaleTone toneMixed(const UpscaleTone& a, const UpscaleTone& b, float share) {
	return a + (b - a) * share;
}

UpscaleTone toneSpread(const UpscaleTone& a, float s) {
	return { a.luma + s, a.orange + s, a.green + s };
}

UpscaleTone toneOf(const Colour& linear) {
	const float red   = std::min(float(linear.red), upscaleCeiling);
	const float green = std::min(float(linear.green), upscaleCeiling);
	const float blue  = std::min(float(linear.blue), upscaleCeiling);
	const float scale = 1 / (1 + std::max(red, std::max(green, blue)));
	const float r     = red * scale;
	const float g     = green * scale;
	const float b     = blue * scale;
	return { 0.25f * r + 0.5f * g + 0.25f * b, 0.5f * r - 0.5f * b, -0.25f * r + 0.5f * g - 0.25f * b };
}

Colour untonedColour(const UpscaleTone& tone) {
	const float r     = std::max(tone.luma + tone.orange - tone.green, 0.0f);
	const float g     = std::max(tone.luma + tone.green, 0.0f);
	const float b     = std::max(tone.luma - tone.orange - tone.green, 0.0f);
	const float scale = 1 / std::max(1 - std::max(r, std::max(g, b)), upscaleFloor);
	return { double(r * scale), double(g * scale), double(b * scale) };
}

void eachUpscaleRow(const UpscaleRows& rows) {
	inParallel(rows.threads, std::size_t(std::max(rows.count, 0)), [&](std::size_t y) {
		rows.row(int(y));
	});
}

float upscaleSampleDepth(const ShownFrame& frame, std::size_t at, std::size_t count) {
	if (frame.surfaces.size() != count) {
		return upscaleSky;
	}
	const float distance = frame.surfaces[at].distance;
	return std::isfinite(distance) ? distance : upscaleSky;
}

void upscaleSampleRow(const Image& internal, const ShownFrame& frame, int y, std::vector<UpscaleSample>& samples) {
	const std::size_t count = samples.size();
	for (int x = 0; x < internal.width; ++x) {
		const std::size_t at = std::size_t(y) * std::size_t(internal.width) + std::size_t(x);

		samples[at] = {
			.tone  = toneOf(colourFrom(internal.rgb.data() + at * 3)),
			.depth = upscaleSampleDepth(frame, at, count),
		};
	}
}

std::vector<UpscaleSample> upscaleSamples(const Image& internal, const ShownFrame& frame) {
	std::vector<UpscaleSample> samples(std::size_t(internal.width) * std::size_t(internal.height));
	const auto row = [&](int y) {
		upscaleSampleRow(internal, frame, y, samples);
	};

	const UpscaleRows rows = {
		.count   = internal.height,
		.threads = frame.threads,
		.row     = row,
	};

	eachUpscaleRow(rows);
	return samples;
}

const UpscaleSample& upscaleSampleAt(const UpscaleTask& task, int x, int y) {
	return task.samples[std::size_t(y) * std::size_t(task.width) + std::size_t(x)];
}

double catmullRom(double t) {
	const double a = std::abs(t);
	if (a < 1) {
		return (1.5 * a - 2.5) * a * a + 1;
	}
	if (a < 2) {
		return ((-0.5 * a + 2.5) * a - 4) * a + 2;
	}
	return 0;
}

UpscaleStats emptyUpscaleStats() {
	const float big = std::numeric_limits<float>::max();
	return UpscaleStats {
		.mean      = { 0, 0, 0 },
		.deviation = { 0, 0, 0 },
		.least     = { big, big, big },
		.most      = { -big, -big, -big },
		.closest   = upscaleSky,
		.farthest  = 0,
	};
}

const UpscaleSample& upscaleGridSample(const UpscaleGrid& grid, int x, int y) {
	const int column = std::clamp(x, 0, grid.width - 1);
	const int row    = std::clamp(y, 0, grid.height - 1);
	return grid.samples[std::size_t(row) * std::size_t(grid.width) + std::size_t(column)];
}

UpscaleStats upscaleStatsAt(const UpscaleGrid& grid, int x, int y) {
	UpscaleStats stats  = emptyUpscaleStats();
	UpscaleTone squares = { 0, 0, 0 };
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			const UpscaleSample& sample = upscaleGridSample(grid, x + dx, y + dy);
			stats.mean                  = stats.mean + sample.tone;
			squares                     = squares + toneSquared(sample.tone);
			stats.least                 = toneLeast(stats.least, sample.tone);
			stats.most                  = toneMost(stats.most, sample.tone);
			stats.closest               = std::min(stats.closest, sample.depth);
			if (sample.depth < upscaleSky) {
				stats.farthest = std::max(stats.farthest, sample.depth);
			}
		}
	}
	stats.mean      = stats.mean * (1.0f / 9);
	stats.deviation = toneRoot(squares * (1.0f / 9) - toneSquared(stats.mean));
	return stats;
}

std::vector<UpscaleStats> upscaleBoxes(const UpscaleGrid& grid, int threads) {
	const int width = grid.width + 2;
	std::vector<UpscaleStats> boxes(std::size_t(width) * std::size_t(grid.height + 2), emptyUpscaleStats());
	const auto row = [&](int y) {
		for (int x = 0; x < width; ++x) {
			boxes[std::size_t(y) * std::size_t(width) + std::size_t(x)] = upscaleStatsAt(grid, x - 1, y - 1);
		}
	};

	const UpscaleRows rows = {
		.count   = grid.height + 2,
		.threads = threads,
		.row     = row,
	};

	eachUpscaleRow(rows);
	return boxes;
}

const UpscaleStats& upscaleBoxAt(const UpscaleTask& task, const UpscaleSpot& spot) {
	return task.boxes[std::size_t(spot.row.box) * std::size_t(task.width + 2) + std::size_t(spot.column.box)];
}

UpscaleGather upscaleGather(const UpscaleTask& task, const UpscaleSpot& spot) {
	const UpscaleStats& stats = upscaleBoxAt(task, spot);
	UpscaleTone sharp         = { 0, 0, 0 };
	UpscaleTone fill          = { 0, 0, 0 };
	float sharpWeight         = 0;
	float fillWeight          = 0;
	for (int dy = 0; dy < 3; ++dy) {
		for (int dx = 0; dx < 3; ++dx) {
			const UpscaleTone& tone = upscaleSampleAt(task, spot.column.taps[dx], spot.row.taps[dy]).tone;
			const float near        = spot.column.sharp[dx] * spot.row.sharp[dy];
			const float spread      = spot.column.fill[dx] * spot.row.fill[dy];
			sharp                   = sharp + tone * near;
			fill                    = fill + tone * spread;
			sharpWeight += near;
			fillWeight += spread;
		}
	}
	return UpscaleGather {
		.sharp       = sharp,
		.sharpWeight = sharpWeight,
		.fill        = toneClamped(fill * (1 / fillWeight), stats.least, stats.most),
		.stats       = stats,
	};
}

math::Vector upscaleHeading(const Lens& lens, double x, double y) {
	const double u = 2 * x / double(lens.width) - 1;
	const double v = 1 - 2 * y / double(lens.height);
	return math::normalized(lens.forward + lens.right * (u * lens.reach * lens.aspect) + lens.up * (v * lens.reach));
}

UpscaleSeen unseen() {
	return UpscaleSeen {
		.inside = false,
		.x      = 0,
		.y      = 0,
		.depth  = 0,
	};
}

UpscaleSeen seenFrom(const Lens& before, const math::Vector& along, float depth) {
	const double ahead = math::dot(along, before.forward);
	if (!(ahead > 1e-9)) {
		return unseen();
	}
	const double u = math::dot(along, before.right) / (ahead * before.reach * before.aspect);
	const double v = math::dot(along, before.up) / (ahead * before.reach);
	const double x = (u + 1) * 0.5 * double(before.width);
	const double y = (1 - v) * 0.5 * double(before.height);
	if (!(x >= 0 && x < double(before.width) && y >= 0 && y < double(before.height))) {
		return unseen();
	}
	return UpscaleSeen {
		.inside = true,
		.x      = x,
		.y      = y,
		.depth  = depth,
	};
}

UpscaleSeen seenBefore(const UpscaleTask& task, const UpscaleSpot& spot, float closest) {
	const math::Vector direction = upscaleHeading(task.frame.lens, double(spot.x) + 0.5, double(spot.y) + 0.5);
	if (!(closest < upscaleSky)) {
		return seenFrom(task.frame.before, direction, upscaleSky);
	}
	const math::Vector along = task.frame.lens.eye + direction * double(closest) - task.frame.before.eye;
	return seenFrom(task.frame.before, along, float(math::length(along)));
}

const UpscalePixel& historyPixel(const UpscaleState& before, int x, int y) {
	const int column = std::clamp(x, 0, before.width - 1);
	const int row    = std::clamp(y, 0, before.height - 1);
	return before.pixels[std::size_t(row) * std::size_t(before.width) + std::size_t(column)];
}

bool depthMatches(float last, float depth, float slack) {
	if (!(depth < upscaleSky)) {
		return !(last < upscaleSky);
	}
	return std::abs(last - depth) <= upscaleDepthShare * depth + slack;
}

bool sameDepth(const UpscaleState& before, const UpscaleSeen& seen, float slack) {
	const int x = int(std::floor(seen.x - 0.5));
	const int y = int(std::floor(seen.y - 0.5));
	for (int dy = 0; dy <= 1; ++dy) {
		for (int dx = 0; dx <= 1; ++dx) {
			if (depthMatches(historyPixel(before, x + dx, y + dy).depth, seen.depth, slack)) {
				return true;
			}
		}
	}
	return false;
}

double historyShare(double at) {
	return at - 0.5 - std::floor(at - 0.5);
}

int historyIndex(double at) {
	return int(std::floor(at - 0.5));
}

Colour historyAcross(const UpscaleState& before, double x, int y) {
	const double share = historyShare(x);
	const int column   = historyIndex(x);
	return historyPixel(before, column, y).colour * (1 - share) + historyPixel(before, column + 1, y).colour * share;
}

Colour historyDown(const UpscaleState& before, int x, double y) {
	const double share = historyShare(y);
	const int row      = historyIndex(y);
	return historyPixel(before, x, row).colour * (1 - share) + historyPixel(before, x, row + 1).colour * share;
}

Colour bilinearHistory(const UpscaleState& before, double x, double y) {
	const double share = historyShare(y);
	const int row      = historyIndex(y);
	return historyAcross(before, x, row) * (1 - share) + historyAcross(before, x, row + 1) * share;
}

UpscaleTaps upscaleTaps(double at) {
	const double centre = std::floor(at - 0.5) + 0.5;
	const double f      = at - centre;
	const double w0     = f * (-0.5 + f * (1 - 0.5 * f));
	const double w1     = 1 + f * f * (-2.5 + 1.5 * f);
	const double w2     = f * (0.5 + f * (2 - 1.5 * f));
	const double w3     = f * f * (-0.5 + 0.5 * f);
	return UpscaleTaps {
		.before       = int(centre - 1.5),
		.middle       = centre + w2 / (w1 + w2),
		.after        = int(centre + 1.5),
		.middleWeight = w1 + w2,
		.beforeWeight = w0,
		.afterWeight  = w3,
	};
}

Colour historyAt(const UpscaleState& before, const UpscaleSeen& seen) {
	const UpscaleTaps across = upscaleTaps(seen.x);
	const UpscaleTaps down   = upscaleTaps(seen.y);
	const Colour top         = historyAcross(before, across.middle, down.before) * (across.middleWeight * down.beforeWeight);
	const Colour left        = historyDown(before, across.before, down.middle) * (across.beforeWeight * down.middleWeight);
	const Colour middle      = bilinearHistory(before, across.middle, down.middle) * (across.middleWeight * down.middleWeight);
	const Colour right       = historyDown(before, across.after, down.middle) * (across.afterWeight * down.middleWeight);
	const Colour bottom      = historyAcross(before, across.middle, down.after) * (across.middleWeight * down.afterWeight);
	const double ends        = down.beforeWeight + down.middleWeight + down.afterWeight;
	const double total       = across.middleWeight * ends + down.middleWeight * (across.beforeWeight + across.afterWeight);
	const Colour sum         = (top + left + middle + right + bottom) * (1 / total);
	return { std::max(sum.red, 0.0), std::max(sum.green, 0.0), std::max(sum.blue, 0.0) };
}

UpscaleBox nearestBox(const UpscaleTask& task, const UpscaleSpot& spot) {
	const int x          = spot.column.taps[1];
	const int y          = spot.row.taps[1];
	const UpscaleTone& a = upscaleSampleAt(task, x, y).tone;
	const UpscaleTone& b = upscaleSampleAt(task, spot.column.toward, y).tone;
	const UpscaleTone& c = upscaleSampleAt(task, x, spot.row.toward).tone;
	const UpscaleTone& e = upscaleSampleAt(task, spot.column.toward, spot.row.toward).tone;
	return UpscaleBox {
		.least = toneLeast(toneLeast(a, b), toneLeast(c, e)),
		.most  = toneMost(toneMost(a, b), toneMost(c, e)),
	};
}

UpscaleTone clampedHistory(const UpscaleTask& task, const UpscaleNear& near, const UpscaleHeld& held) {
	const float spread     = upscaleClampStill + (upscaleClamp - upscaleClampStill) * held.motion;
	const float margin     = upscaleStillMargin * (1 - held.motion);
	const UpscaleTone low  = toneSpread(near.gather.stats.mean - near.gather.stats.deviation * spread, -margin);
	const UpscaleTone high = toneSpread(near.gather.stats.mean + near.gather.stats.deviation * spread, margin);
	if (!(held.motion > 0)) {
		return toneClamped(held.history, low, high);
	}
	const UpscaleBox box        = nearestBox(task, near.spot);
	const UpscaleTone tightLow  = toneMixed(low, toneMost(low, box.least), held.motion);
	const UpscaleTone tightHigh = toneMixed(high, toneLeast(high, box.most), held.motion);
	return toneClamped(held.history, tightLow, tightHigh);
}

UpscaleTone blendedTone(const UpscaleTask& task, const UpscaleNear& near, const UpscaleSeen& seen) {
	const double movedX     = seen.x - (double(near.spot.x) + 0.5);
	const double movedY     = seen.y - (double(near.spot.y) + 0.5);
	const double moved      = std::sqrt(movedX * movedX + movedY * movedY);
	const UpscaleTone toned = toneOf(historyAt(task.before, seen));

	const UpscaleHeld held = {
		.history = toned,
		.motion  = float(std::min(moved / upscaleStill, 1.0)),
	};

	const UpscaleTone last  = clampedHistory(task, near, held);
	const float speed       = float(std::min(moved / upscaleFast, 1.0));
	const float base        = task.settling ? upscaleAlphaSettle : upscaleAlpha;
	const float alpha       = std::clamp((base + (upscaleAlphaFast - base) * speed) * near.gather.sharpWeight, upscaleAlphaLeast, 1.0f);
	const UpscaleTone sharp = near.gather.sharp * (1 / std::max(near.gather.sharpWeight, upscaleLeastWeight));
	const UpscaleTone blend = toneMixed(last, sharp, alpha);
	return toneMixed(blend, near.gather.fill, upscaleAlphaMoving * speed);
}

float upscaleSlack(const UpscaleGather& gather) {
	return std::min(gather.stats.farthest - gather.stats.closest, upscaleSpreadShare * gather.stats.closest);
}

UpscaleTone upscaledTone(const UpscaleTask& task, const UpscaleNear& near) {
	if (!task.kept) {
		return near.gather.fill;
	}
	const UpscaleSeen seen = seenBefore(task, near.spot, near.gather.stats.closest);
	if (!seen.inside || !sameDepth(task.before, seen, upscaleSlack(near.gather))) {
		return near.gather.fill;
	}
	return blendedTone(task, near, seen);
}

void upscaledRow(const UpscaleTask& task, int y, Upscaled& next) {
	const int width = task.frame.width;
	for (int x = 0; x < width; ++x) {
		const UpscaleSpot spot = {
			.x      = x,
			.y      = y,
			.column = task.columns[std::size_t(x)],
			.row    = task.rows[std::size_t(y)],
		};

		const UpscaleNear near = {
			.spot   = spot,
			.gather = upscaleGather(task, spot),
		};

		const Colour linear  = untonedColour(upscaledTone(task, near));
		const std::size_t at = std::size_t(y) * std::size_t(width) + std::size_t(x);

		next.state.pixels[at] = {
			.colour = linear,
			.depth  = near.gather.stats.closest,
		};

		next.image.rgb[at * 3]     = float(linear.red);
		next.image.rgb[at * 3 + 1] = float(linear.green);
		next.image.rgb[at * 3 + 2] = float(linear.blue);
	}
}

bool keptUpscale(const UpscaleState& before, const ShownFrame& frame) {
	const std::size_t count = std::size_t(frame.width) * std::size_t(frame.height);
	const bool sized        = before.width == frame.width && before.height == frame.height && before.width > 0;
	return !frame.restarted && sized && before.pixels.size() == count;
}

int settlingFrames(const UpscaleState& before, const ShownFrame& frame) {
	if (frame.moved) {
		return 0;
	}
	return before.moved ? upscaleSettleFrames : before.settling;
}

int towardSpot(double offset) {
	return offset > 0 ? -1 : offset < 0 ? 1
										: 0;
}

UpscaleAxis upscaleAxis(const UpscaleLine& line, int shown) {
	const double internal = (double(shown) + 0.5) / double(line.factor);
	const int nearest     = int(std::floor(internal - line.jitter));
	const double first    = double(nearest) + 0.5 + line.jitter - internal;

	UpscaleAxis axis = {
		.taps   = { 0, 0, 0 },
		.sharp  = { 0, 0, 0 },
		.fill   = { 0, 0, 0 },
		.toward = 0,
		.box    = 0,
	};

	for (int d = 0; d < 3; ++d) {
		const int at        = std::clamp(nearest + d - 1, 0, line.count - 1);
		const double offset = (double(at) + 0.5 + line.jitter - internal) * double(line.factor);
		axis.taps[d]        = at;
		axis.sharp[d]       = float(std::exp(-upscaleSharp * offset * offset));
		axis.fill[d]        = float(catmullRom(first + double(d - 1)));
	}
	const double centre = double(axis.taps[1]) + 0.5 + line.jitter - internal;
	axis.toward         = std::clamp(axis.taps[1] + towardSpot(centre), 0, line.count - 1);
	axis.box            = nearest + 1;
	return axis;
}

std::vector<UpscaleAxis> upscaleAxes(const UpscaleLine& line, int shown) {
	std::vector<UpscaleAxis> axes;
	axes.reserve(std::size_t(shown));
	for (int k = 0; k < shown; ++k) {
		axes.push_back(upscaleAxis(line, k));
	}
	return axes;
}

double upscaleJitter(int place, int factor) {
	return (double(place) + 0.5) / double(factor) - 0.5;
}

std::vector<UpscalePixel> spareUpscalePixels(std::vector<UpscalePixel> spare, std::size_t count) {
	spare.resize(count);
	return spare;
}

UpscaleState emptyUpscale() {
	return UpscaleState {
		.width    = 0,
		.height   = 0,
		.pixels   = {},
		.spare    = {},
		.moved    = false,
		.settling = 0,
	};
}

Upscaled upscaledFrame(UpscaleState before, const Image& internal, const ShownFrame& frame) {
	const int factor = frame.film.factor;
	if (factor <= 1) {
		return Upscaled {
			.image = internal,
			.state = emptyUpscale(),
		};
	}
	const std::vector<UpscaleSample> samples = upscaleSamples(internal, frame);
	const bool kept                          = keptUpscale(before, frame);
	const int settling                       = kept ? settlingFrames(before, frame) : 0;

	const UpscaleLine across = {
		.factor = factor,
		.jitter = upscaleJitter(frame.film.phase % factor, factor),
		.count  = internal.width,
	};

	const UpscaleLine down = {
		.factor = factor,
		.jitter = upscaleJitter(frame.film.phase / factor, factor),
		.count  = internal.height,
	};

	const std::vector<UpscaleAxis> columnAxes = upscaleAxes(across, frame.width);
	const std::vector<UpscaleAxis> rowAxes    = upscaleAxes(down, frame.height);

	const UpscaleGrid grid = {
		.samples = samples,
		.width   = internal.width,
		.height  = internal.height,
	};

	const std::vector<UpscaleStats> boxes = upscaleBoxes(grid, frame.threads);

	const UpscaleTask task = {
		.before   = before,
		.frame    = frame,
		.samples  = samples,
		.columns  = columnAxes,
		.rows     = rowAxes,
		.boxes    = boxes,
		.width    = internal.width,
		.kept     = kept,
		.settling = settling > 0,
	};

	Upscaled next = {
		.image = blankImage(frame.width, frame.height),

		.state = {
			.width    = frame.width,
			.height   = frame.height,
			.pixels   = spareUpscalePixels(std::move(before.spare), std::size_t(frame.width) * std::size_t(frame.height)),
			.spare    = {},
			.moved    = frame.moved,
			.settling = std::max(settling - 1, 0),
		},
	};

	const auto row = [&](int y) {
		upscaledRow(task, y, next);
	};

	const UpscaleRows rows = {
		.count   = frame.height,
		.threads = frame.threads,
		.row     = row,
	};

	eachUpscaleRow(rows);
	next.state.spare = std::move(before.pixels);
	return next;
}

std::shared_ptr<void> openedUpscaleState() {
	return std::make_shared<UpscaleState>(emptyUpscale());
}

UpscaleState& upscaleState(States& states) {
	return *static_cast<UpscaleState*>(heldState(states, "reconstruction/upscale", openedUpscaleState));
}

}
