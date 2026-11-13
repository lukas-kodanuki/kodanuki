#include "plugins/sampler/sky_map/sky_map.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

namespace kodanuki {

constexpr std::size_t chartRows      = 64;
constexpr std::size_t chartColumns   = 128;
constexpr double rowHeight           = 2.0 / chartRows;
constexpr double columnWidth         = 2 * math::pi / chartColumns;
constexpr double evenShare           = 0.1;
constexpr double outshoneRatio       = 5;
constexpr std::size_t cellsAcrossRow = chartColumns + 1;

SkyChart unchartedSky() {
	return SkyChart {
		.charted    = false,
		.up         = { 0, 0, 1 },
		.level      = { 1, 0, 0 },
		.aside      = { 0, 1, 0 },
		.rows       = {},
		.cells      = {},
		.brightness = 0,
		.even       = evenShare,
	};
}

math::Vector chartDirection(const SkyChart& chart, double height, double turn) {
	const double across = std::sqrt(std::max(1 - height * height, 0.0));
	return chart.level * (across * std::cos(turn)) + chart.aside * (across * std::sin(turn)) + chart.up * height;
}

double skyBrightness(const SkyChartQuery& query, const math::Vector& direction) {
	return std::max(channelMean(skyColourAt(query.features, query.lights.sky, direction)), 0.0);
}

std::vector<double> cellWeights(const SkyChart& chart, const SkyChartQuery& query) {
	std::vector<double> weights(chartRows * chartColumns, 0);
	for (std::size_t row = 0; row < chartRows; ++row) {
		const double height = -1 + (double(row) + 0.5) * rowHeight;
		for (std::size_t column = 0; column < chartColumns; ++column) {
			const double turn                    = (double(column) + 0.5) * columnWidth;
			weights[row * chartColumns + column] = skyBrightness(query, chartDirection(chart, height, turn));
		}
	}
	return weights;
}

double runOfRow(SkyChart& chart, const std::vector<double>& weights, std::size_t row) {
	double* run = &chart.cells[row * cellsAcrossRow];
	for (std::size_t column = 0; column < chartColumns; ++column) {
		run[column + 1] = run[column] + weights[row * chartColumns + column];
	}
	const double held = run[chartColumns];
	for (std::size_t column = 1; column <= chartColumns; ++column) {
		run[column] = held > 0 ? run[column] / held : double(column) / chartColumns;
	}
	return held;
}

SkyChart filledChart(const SkyChart& empty, const std::vector<double>& weights, double total) {
	SkyChart chart = empty;
	chart.charted  = true;
	chart.rows.assign(chartRows + 1, 0);
	chart.cells.assign(chartRows * cellsAcrossRow, 0);
	for (std::size_t row = 0; row < chartRows; ++row) {
		chart.rows[row + 1] = chart.rows[row] + runOfRow(chart, weights, row) / total;
	}
	chart.rows[chartRows] = 1;
	chart.brightness      = total / double(chartRows * chartColumns);
	return chart;
}

double lampSent(const SkyChartQuery& query, std::uint32_t light) {
	const Lamp& lamp = query.lights.lamps[light];
	if (lamp.kind == LightKind::Directional) {
		return std::max(channelMean(lamp.power), 0.0);
	}
	if (lamp.kind == LightKind::Sun || lamp.kind == LightKind::Moon) {
		return std::max(channelMean(lamp.power), 0.0) * lampThinning(query.features, light, query.middle);
	}
	const math::Vector place = lamp.kind == LightKind::Quad ? lamp.at + (lamp.along + lamp.across) * 0.5 : lamp.at;
	const math::Vector apart = place - query.middle;
	return lamp.emitted / (4 * math::pi * std::max(math::dot(apart, apart), 1e-12));
}

bool outshone(const SkyChartQuery& query, const SkyChart& chart) {
	double lit = 0;
	for (std::uint32_t light = 0; light < query.lights.lamps.size(); ++light) {
		lit += lampSent(query, light);
	}
	return lit > outshoneRatio * 4 * math::pi * chart.brightness;
}

SkyChart chartedSky(const SkyChartQuery& query) {
	SkyChart chart                   = unchartedSky();
	chart.up                         = math::normalized(query.lights.sky.up);
	const math::Basis basis          = math::basisAround(chart.up);
	chart.level                      = basis.tangent;
	chart.aside                      = basis.bitangent;
	const std::vector<double> weight = cellWeights(chart, query);
	double total                     = 0;
	for (const double one : weight) {
		total += one;
	}
	if (!(total > 0)) {
		return unchartedSky();
	}
	const SkyChart filled = filledChart(chart, weight, total);
	return outshone(query, filled) ? unchartedSky() : filled;
}

std::size_t foundIn(const double* run, std::size_t count, double target) {
	const double* past = std::upper_bound(run + 1, run + count + 1, target);
	return std::min(std::size_t(past - run - 1), count - 1);
}

math::Vector chartedDirection(const SkyChart& chart, const SkyDraw& draw) {
	if (draw.part < chart.even) {
		return chartDirection(chart, 1 - 2 * draw.pair.u, 2 * math::pi * draw.pair.v);
	}
	const std::size_t row    = foundIn(chart.rows.data(), chartRows, draw.pair.u);
	const std::size_t column = foundIn(&chart.cells[row * cellsAcrossRow], chartColumns, draw.pair.v);
	const double height      = -1 + (double(row) + draw.jitter.u) * rowHeight;
	const double turn        = (double(column) + draw.jitter.v) * columnWidth;
	return chartDirection(chart, height, turn);
}

LightAim chartAim(const SkyChart& chart, const SkyDraw& draw) {
	const math::Vector toward = math::normalized(chartedDirection(chart, draw));
	const double density      = chartDensity(chart, toward);
	const Spectrum4 sent      = skyRadiance(draw.sky, toward, draw.wavelengths);
	return LightAim {
		.found    = density > 0,
		.toward   = toward,
		.distance = std::numeric_limits<double>::infinity(),
		.radiance = density > 0 ? sent / density : flatSpectrum(0),
		.raw      = sent,
		.density  = density,
		.cell     = noCell,
	};
}

double chartDensity(const SkyChart& chart, const math::Vector& toward) {
	const double height      = std::clamp(math::dot(toward, chart.up), -1.0, 1.0);
	const double angle       = std::atan2(math::dot(toward, chart.aside), math::dot(toward, chart.level));
	const double turn        = angle < 0 ? angle + 2 * math::pi : angle;
	const std::size_t row    = std::min(std::size_t((height + 1) / rowHeight), chartRows - 1);
	const std::size_t column = std::min(std::size_t(turn / columnWidth), chartColumns - 1);
	const double* run        = &chart.cells[row * cellsAcrossRow];
	const double held        = (chart.rows[row + 1] - chart.rows[row]) * (run[column + 1] - run[column]);
	return chart.even / (4 * math::pi) + (1 - chart.even) * held / (rowHeight * columnWidth);
}

std::shared_ptr<void> openedSkyChart() {
	return std::make_shared<SkyChart>(unchartedSky());
}

SkyChart& skyChartState(States& states) {
	return *static_cast<SkyChart*>(heldState(states, "sampler/sky_map", openedSkyChart));
}

}
