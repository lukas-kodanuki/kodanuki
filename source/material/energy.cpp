#include "material/energy.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kodanuki {

struct FacetGrid {
	int rings;
	int spokes;
	double width;
};

struct LaidGrid {
	FacetGrid grid;
	std::vector<math::Vector> facets;
};

struct SeenFacet {
	math::Vector facet;
	double onto;
	double bounced;
	double share;
	double visibility;
};

struct LaidView {
	FacetGrid grid;
	math::Vector outgoing;
	std::vector<SeenFacet> facets;
};

struct SingleBounce {
	double returned;
	double passed;
};

struct TableRow {
	int lower;
	int upper;
	double share;
};

math::Vector facetOnGrid(const FacetGrid& grid, int ring, int spoke) {
	const double along  = (ring + 0.5) / grid.rings;
	const double tilt   = std::sqrt((1 - along) / (1 + (grid.width * grid.width - 1) * along));
	const double spread = std::sqrt(std::max(1 - tilt * tilt, 0.0));
	const double angle  = 2 * math::pi * (spoke + 0.5) / grid.spokes;
	return { spread * std::cos(angle), spread * std::sin(angle), tilt };
}

LaidGrid laidGrid(const FacetGrid& grid) {
	LaidGrid laid = {
		.grid   = grid,
		.facets = {},
	};

	for (int ring = 0; ring < grid.rings; ++ring) {
		for (int spoke = 0; spoke < grid.spokes; ++spoke) {
			laid.facets.push_back(facetOnGrid(grid, ring, spoke));
		}
	}
	return laid;
}

math::Vector viewAt(double leaving) {
	return { std::sqrt(std::max(1 - leaving * leaving, 0.0)), 0, leaving };
}

double facetReflection(const FacetGrid& grid, const math::Vector& outgoing, const math::Vector& facet) {
	const double onto = math::dot(outgoing, facet);
	if (onto <= 0) {
		return 0;
	}
	const math::Vector incoming = facet * (2 * onto) - outgoing;
	if (incoming.z <= 0) {
		return 0;
	}

	const Cosines cosines = {
		.leaving  = outgoing.z,
		.arriving = incoming.z,
	};

	return 4 * smithVisibility(cosines, grid.width) * incoming.z * onto / facet.z;
}

double escapeAtView(const LaidGrid& laid, double leaving) {
	const math::Vector outgoing = viewAt(leaving);
	double total                = 0;
	for (const math::Vector& facet : laid.facets) {
		total += facetReflection(laid.grid, outgoing, facet);
	}
	return total / (laid.grid.rings * laid.grid.spokes);
}

Escape escapeOf(double width) {
	const FacetGrid grid = {
		.rings  = 24,
		.spokes = 12,
		.width  = width,
	};

	const LaidGrid laid = laidGrid(grid);

	Escape escape = {
		.table   = {},
		.average = 0,
	};

	for (int view = 0; view < energyViews; ++view) {
		const double leaving = (view + 0.5) / energyViews;
		escape.table[view]   = escapeAtView(laid, leaving);
		escape.average += 2 * escape.table[view] * leaving / energyViews;
	}
	return escape;
}

double escapeAt(const Escape& escape, double cosine) {
	const int last     = energyViews - 1;
	const double place = std::clamp(cosine, 0.0, 1.0) * energyViews - 0.5;
	const int lower    = std::clamp(int(std::floor(place)), 0, last);
	const int upper    = std::min(lower + 1, last);
	const double share = std::clamp(place - lower, 0.0, 1.0);
	return escape.table[lower] * (1 - share) + escape.table[upper] * share;
}

double compensationLobe(const Escape& escape, const Cosines& cosines) {
	const double room = 1 - escape.average;
	if (room <= 1e-6) {
		return 0;
	}
	const double left  = 1 - escapeAt(escape, cosines.leaving);
	const double right = 1 - escapeAt(escape, cosines.arriving);
	return left * right / (math::pi * room);
}

SeenFacet seenFacet(const FacetGrid& grid, const math::Vector& outgoing, const math::Vector& facet) {
	const double onto          = math::dot(outgoing, facet);
	const math::Vector bounced = facet * (2 * onto) - outgoing;

	const Cosines reflected = {
		.leaving  = outgoing.z,
		.arriving = bounced.z,
	};

	return SeenFacet {
		.facet      = facet,
		.onto       = onto,
		.bounced    = bounced.z,
		.share      = onto / facet.z,
		.visibility = smithVisibility(reflected, grid.width),
	};
}

LaidView laidView(const LaidGrid& laid, double leaving) {
	LaidView view = {
		.grid     = laid.grid,
		.outgoing = viewAt(leaving),
		.facets   = {},
	};

	for (const math::Vector& facet : laid.facets) {
		view.facets.push_back(seenFacet(laid.grid, view.outgoing, facet));
	}
	return view;
}

SingleBounce facetBounce(const LaidView& view, const SeenFacet& seen, double ratio) {
	if (seen.onto <= 0) {
		return { 0, 0 };
	}
	const double back           = dielectricReflectance(seen.onto, ratio);
	const Refraction refraction = refractedThrough(seen.facet, view.outgoing, ratio);

	const Cosines passed = {
		.leaving  = view.outgoing.z,
		.arriving = -refraction.direction.z,
	};

	const bool returns = seen.bounced > 0;
	const bool passes  = back < 1 && refraction.passed && refraction.direction.z < 0;
	const double kept  = returns ? back * 4 * seen.visibility * seen.bounced * seen.share : 0;
	const double went  = passes ? (1 - back) * 4 * smithVisibility(passed, view.grid.width) * -refraction.direction.z * seen.share : 0;
	return { kept, went };
}

SingleBounce bounceAtView(const LaidView& view, double ratio) {
	SingleBounce total = { 0, 0 };
	for (const SeenFacet& seen : view.facets) {
		const SingleBounce one = facetBounce(view, seen, ratio);
		total                  = { total.returned + one.returned, total.passed + one.passed };
	}
	const double count = view.grid.rings * view.grid.spokes;
	return { total.returned / count, total.passed / count };
}

double ratioOfRow(int row) {
	return 1 + (densestRatio - 1) * row / (boundaryRatios - 1);
}

double throughShare(const Boundary& boundary, int row, double passed) {
	const double ratio    = ratioOfRow(row);
	const double rarer    = boundary.rarerAverage[row];
	const double nearLost = 1 - rarer;
	const double farLost  = 1 - boundary.denserAverage[row];
	const bool bothLose   = nearLost > 1e-9 && farLost > 1e-9 && rarer > 0;
	return bothLose ? std::min(passed / rarer, farLost * ratio * ratio / nearLost) : 0;
}

void fillBoundaryRow(Boundary& boundary, const std::vector<LaidView>& views, int row) {
	const double ratio = ratioOfRow(row);
	double passed      = 0;
	for (int view = 0; view < energyViews; ++view) {
		const double leaving          = (view + 0.5) / energyViews;
		const SingleBounce fromRarer  = bounceAtView(views[view], 1 / ratio);
		const SingleBounce fromDenser = bounceAtView(views[view], ratio);
		const int cell                = row * energyViews + view;
		boundary.rarer[cell]          = fromRarer.returned + fromRarer.passed;
		boundary.denser[cell]         = fromDenser.returned + fromDenser.passed;
		boundary.rarerAverage[row] += 2 * boundary.rarer[cell] * leaving / energyViews;
		boundary.denserAverage[row] += 2 * boundary.denser[cell] * leaving / energyViews;
		passed += 2 * fromRarer.passed * leaving / energyViews;
	}
	boundary.rarerThrough[row] = throughShare(boundary, row, passed);
}

Boundary boundaryOf(double width) {
	const FacetGrid grid = {
		.rings  = 32,
		.spokes = 16,
		.width  = width,
	};

	const LaidGrid laid = laidGrid(grid);
	std::vector<LaidView> views;
	for (int view = 0; view < energyViews; ++view) {
		views.push_back(laidView(laid, (view + 0.5) / energyViews));
	}

	Boundary boundary = {
		.rarer         = {},
		.denser        = {},
		.rarerAverage  = {},
		.denserAverage = {},
		.rarerThrough  = {},
	};

	for (int row = 0; row < boundaryRatios; ++row) {
		fillBoundaryRow(boundary, views, row);
	}
	return boundary;
}

TableRow rowOf(const Indices& indices) {
	const double ratio = std::max(indices.near, indices.far) / std::min(indices.near, indices.far);
	const double place = (std::min(ratio, densestRatio) - 1) / (densestRatio - 1) * (boundaryRatios - 1);
	const int lower    = std::clamp(int(std::floor(place)), 0, boundaryRatios - 1);

	const TableRow row = {
		.lower = lower,
		.upper = std::min(lower + 1, boundaryRatios - 1),
		.share = std::clamp(place - lower, 0.0, 1.0),
	};

	return row;
}

double blendedRow(const std::array<double, boundaryRatios>& values, const TableRow& row) {
	return values[row.lower] * (1 - row.share) + values[row.upper] * row.share;
}

Loss lossless(double nearAverage, double farAverage) {
	return Loss {
		.nearAverage = nearAverage,
		.farAverage  = farAverage,
		.back        = 0,
		.through     = 0,
		.share       = 0,
	};
}

Loss lossAt(const Boundary& boundary, const Indices& indices) {
	const TableRow row       = rowOf(indices);
	const double rarer       = blendedRow(boundary.rarerAverage, row);
	const double denser      = blendedRow(boundary.denserAverage, row);
	const double share       = blendedRow(boundary.rarerThrough, row);
	const bool fromRarer     = indices.near <= indices.far;
	const double nearAverage = fromRarer ? rarer : denser;
	const double farAverage  = fromRarer ? denser : rarer;
	const double nearLost    = 1 - nearAverage;
	const double denserLost  = 1 - denser;
	if (!(nearLost > 1e-9) || !(denserLost > 1e-9)) {
		return lossless(nearAverage, farAverage);
	}
	const double turned  = (indices.far / indices.near) * (indices.far / indices.near);
	const double through = fromRarer ? share / denserLost : share / denserLost * turned;
	const double given   = std::min(through * (1 - farAverage), 1.0);
	return Loss {
		.nearAverage = nearAverage,
		.farAverage  = farAverage,
		.back        = (1 - given) / nearLost,
		.through     = through,
		.share       = given,
	};
}

double rowAt(const std::array<double, boundaryRatios * energyViews>& table, int row, double cosine) {
	const double place = std::clamp(cosine, 0.0, 1.0) * energyViews - 0.5;
	const int lower    = std::clamp(int(std::floor(place)), 0, energyViews - 1);
	const int upper    = std::min(lower + 1, energyViews - 1);
	const double share = std::clamp(place - lower, 0.0, 1.0);
	return table[row * energyViews + lower] * (1 - share) + table[row * energyViews + upper] * share;
}

double boundaryAt(const Boundary& boundary, const BoundaryReading& reading) {
	const TableRow row                                            = rowOf(reading.indices);
	const std::array<double, boundaryRatios * energyViews>& table = reading.indices.near <= reading.indices.far ? boundary.rarer : boundary.denser;
	return rowAt(table, row.lower, reading.cosine) * (1 - row.share) + rowAt(table, row.upper, reading.cosine) * row.share;
}

}
