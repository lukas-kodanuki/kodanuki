#include "extras/reconstruction_biased_upsample/upsample.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace kodanuki {

struct UpsampleTap {
	int first;
	int second;
	float share;
};

UpsampleTap upsampleTap(int shown, int factor, int drawn) {
	const double centre  = (double(shown) + 0.5) / double(factor) - 0.5;
	const double clamped = std::clamp(centre, 0.0, double(drawn - 1));
	const int first      = int(clamped);
	return UpsampleTap {
		.first  = first,
		.second = std::min(first + 1, drawn - 1),
		.share  = float(clamped - double(first)),
	};
}

std::vector<UpsampleTap> upsampleTaps(int shown, int factor, int drawn) {
	std::vector<UpsampleTap> taps;
	taps.reserve(std::size_t(shown));
	for (int k = 0; k < shown; ++k) {
		taps.push_back(upsampleTap(k, factor, drawn));
	}
	return taps;
}

struct UpsampleRow {
	const Image& drawn;
	const std::vector<UpsampleTap>& columns;
	UpsampleTap row;
};

float blendedChannel(const float* upper, const float* lower, const UpsampleTap& column, float share) {
	const float top    = upper[column.first] + (upper[column.second] - upper[column.first]) * column.share;
	const float bottom = lower[column.first] + (lower[column.second] - lower[column.first]) * column.share;
	return top + (bottom - top) * share;
}

void upsampledRow(const UpsampleRow& row, float* shown) {
	const std::size_t stride = std::size_t(row.drawn.width) * 3;
	const float* upper       = row.drawn.rgb.data() + std::size_t(row.row.first) * stride;
	const float* lower       = row.drawn.rgb.data() + std::size_t(row.row.second) * stride;
	for (std::size_t x = 0; x < row.columns.size(); ++x) {
		const UpsampleTap& column = row.columns[x];

		const UpsampleTap tap = {
			.first  = column.first * 3,
			.second = column.second * 3,
			.share  = column.share,
		};

		for (int channel = 0; channel < 3; ++channel) {
			shown[x * 3 + std::size_t(channel)] = blendedChannel(upper + channel, lower + channel, tap, row.row.share);
		}
	}
}

Image upsampledImage(const Image& drawn, const ShownFrame& frame) {
	const int factor = frame.film.factor;
	if (factor == 1) {
		return drawn;
	}

	const std::vector<UpsampleTap> columns = upsampleTaps(frame.width, factor, drawn.width);
	const std::vector<UpsampleTap> rows    = upsampleTaps(frame.height, factor, drawn.height);
	Image image                            = blankImage(frame.width, frame.height);
	const std::size_t stride               = std::size_t(frame.width) * 3;
	for (std::size_t y = 0; y < rows.size(); ++y) {
		const UpsampleRow row = {
			.drawn   = drawn,
			.columns = columns,
			.row     = rows[y],
		};

		upsampledRow(row, image.rgb.data() + y * stride);
	}
	return image;
}

}
