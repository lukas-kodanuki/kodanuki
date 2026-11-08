#include "transport/weight.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kodanuki {

constexpr double leastCosine = 1e-12;

double infiniteCount(const Densities& densities) {
	return double(std::isinf(densities.meet)) + double(std::isinf(densities.aim)) + double(std::isinf(densities.join));
}

bool atDepthCap(const VertexCounts& counts, const DepthCaps& caps) {
	return counts.surfaces >= caps.surfaces || counts.collisions >= caps.collisions;
}

double weightOf(const Densities& densities, double own) {
	const double total = densities.meet * densities.meet + densities.aim * densities.aim + densities.join * densities.join + densities.connections;
	if (!(total > 0)) {
		return 0;
	}
	if (total < std::numeric_limits<double>::infinity()) {
		return own * own / total;
	}
	return std::isinf(own) && infiniteCount(densities) == 1 ? 1 : 0;
}

Weighing weighingAtLens(const StrategyCounts& counts, double lensDensity) {
	return Weighing {
		.join        = counts.join / lensDensity,
		.connections = 0,
		.cut         = 0,
		.drawn       = 0,
		.relays      = false,
		.aimed       = Aimed::Camera,
	};
}

Weighing weighingAtHit(const Weighing& weighing, const HitFactors& factors) {
	const double cosine = std::max(factors.cosine, leastCosine);
	const double spread = factors.distance * factors.distance;
	const bool camera   = weighing.aimed == Aimed::Camera;
	const double joined = factors.joinable ? weighing.join * spread / cosine : 0;
	Weighing carried    = weighing;
	carried.join        = camera ? joined : weighing.join / cosine;
	carried.connections = weighing.connections / (cosine * cosine);
	carried.cut         = factors.connectable ? weighing.cut * spread * spread / (cosine * cosine) : 0;
	return carried;
}

double countedDensity(double density, bool exact) {
	return exact ? 1 : density;
}

double chainedConnections(const ScatterFactors& factors, double connections, double cut) {
	const double forward = countedDensity(factors.forward, factors.exact);
	const double reverse = countedDensity(factors.reverse, factors.exact);
	const double turned  = factors.cosine * factors.cosine / (forward * forward);
	const double added   = factors.connectable ? cut : 0;
	return factors.exact ? turned * connections : turned * (connections * reverse * reverse + added);
}

double chainedCut(const ScatterFactors& factors) {
	return factors.exact || !factors.connectable ? 0 : 1 / (factors.forward * factors.forward);
}

Weighing weighingAfterScatter(const Weighing& weighing, const ScatterFactors& factors) {
	const double forward = countedDensity(factors.forward, factors.exact);
	const double reverse = weighing.aimed == Aimed::Camera ? 1 : countedDensity(factors.reverse, factors.exact);
	return Weighing {
		.join        = forward > 0 ? weighing.join * reverse * factors.cosine / forward : 0,
		.connections = forward > 0 ? chainedConnections(factors, weighing.connections, weighing.cut) : 0,
		.cut         = forward > 0 ? chainedCut(factors) : 0,
		.drawn       = factors.exact ? 0 : factors.forward,
		.relays      = factors.sharp,
		.aimed       = factors.aimed,
	};
}

Densities meetDensities(const MeetEnding& ending) {
	const Weighing& weighing = ending.weighing;
	const double meet        = weighing.drawn > 0 ? weighing.drawn : 1;
	const double emitted     = ending.emission * meet;
	return Densities {
		.meet        = meet,
		.aim         = ending.aim * ending.counts.aim,
		.join        = weighing.relays ? weighing.join * emitted : 0,
		.connections = ending.counts.connect > 0 ? emitted * emitted * weighing.connections * ending.counts.connect * ending.counts.connect : 0,
	};
}

Densities aimDensities(const AimEnding& ending) {
	const Weighing& weighing = ending.weighing;
	const bool singular      = !(ending.aim > 0);
	const double reverse     = weighing.aimed == Aimed::Camera ? 1 : ending.reverse;
	const double emitted     = ending.emission * ending.cosine;
	const double chain       = weighing.cut + weighing.connections * reverse * reverse;
	return Densities {
		.meet        = singular || ending.capped ? 0 : ending.forward,
		.aim         = (singular ? 1 : ending.aim) * ending.counts.aim,
		.join        = ending.sharp ? weighing.join * reverse * emitted : 0,
		.connections = ending.counts.connect > 0 ? emitted * emitted * chain * ending.counts.connect * ending.counts.connect : 0,
	};
}

bool meetReadsEmission(const Weighing& weighing, const StrategyCounts& counts) {
	return (weighing.relays && weighing.join > 0) || counts.connect > 0;
}

bool aimReadsEmission(const Weighing& weighing, const StrategyCounts& counts, bool sharp) {
	return (sharp && weighing.join > 0) || counts.connect > 0;
}

LightWeighing weighingAtLamp(const LampStart& start, const StrategyCounts& counts) {
	return LightWeighing {
		.meet        = start.singular ? 0 : start.cosine / start.emission,
		.aim         = counts.aim * (start.singular ? 1 : start.cosine) / start.emission,
		.connections = 0,
		.cut         = 0,
		.fresh       = true,
	};
}

LightWeighing lightWeighingAtHit(const LightWeighing& weighing, const LightHitFactors& factors) {
	const double cosine = std::max(factors.cosine, leastCosine);
	const double aimed  = weighing.fresh ? weighing.aim * factors.aim : weighing.aim;
	return LightWeighing {
		.meet        = weighing.meet / cosine,
		.aim         = aimed / cosine,
		.connections = weighing.connections / (cosine * cosine),
		.cut         = factors.connectable ? weighing.cut * std::pow(factors.distance, 4) / (cosine * cosine) : 0,
		.fresh       = weighing.fresh,
	};
}

LightWeighing lightWeighingAfterScatter(const LightWeighing& weighing, const ScatterFactors& factors) {
	const double forward = countedDensity(factors.forward, factors.exact);
	const double reverse = countedDensity(factors.reverse, factors.exact);
	const double turned  = forward > 0 ? factors.cosine / forward : 0;
	return LightWeighing {
		.meet        = weighing.meet * reverse * turned,
		.aim         = weighing.aim * (weighing.fresh ? 1 : reverse) * turned,
		.connections = forward > 0 ? chainedConnections(factors, weighing.connections, weighing.cut) : 0,
		.cut         = forward > 0 ? chainedCut(factors) : 0,
		.fresh       = false,
	};
}

Densities joinDensities(const JoinEnding& ending) {
	const LightWeighing& weighing = ending.weighing;
	const double eye              = ending.forward * ending.lens;
	const double chain            = weighing.cut + weighing.connections * ending.forward * ending.forward;
	return Densities {
		.meet        = ending.capped ? 0 : weighing.meet * eye,
		.aim         = weighing.aim * (weighing.fresh ? ending.lens : eye),
		.join        = ending.counts.join,
		.connections = ending.counts.connect > 0 ? ending.lens * ending.lens * chain * ending.counts.connect * ending.counts.connect : 0,
	};
}

struct EdgeAreas {
	double eye;
	double light;
};

EdgeAreas edgeAreas(const ConnectEnding& ending) {
	const double squared = ending.distance * ending.distance;
	return EdgeAreas {
		.eye   = ending.eyeForward * ending.lightCosine / squared,
		.light = ending.lightForward * ending.eyeCosine / squared,
	};
}

double connectionSquares(const ConnectEnding& ending, const EdgeAreas& areas) {
	const double eyeSide   = areas.light * areas.light * (ending.eye.connections * ending.eyeReverse * ending.eyeReverse + ending.eye.cut);
	const double lightSide = areas.eye * areas.eye * (ending.light.connections * ending.lightReverse * ending.lightReverse + ending.light.cut);
	return ending.counts.connect * ending.counts.connect * (1 + eyeSide + lightSide);
}

Densities connectDensities(const ConnectEnding& ending) {
	const EdgeAreas areas = edgeAreas(ending);
	const double beyond   = ending.lightReverse * areas.eye;
	const double before   = ending.eye.aimed == Aimed::Camera ? 1 : ending.eyeReverse;
	return Densities {
		.meet        = ending.capped ? 0 : ending.light.meet * beyond,
		.aim         = ending.light.aim * (ending.light.fresh ? areas.eye : beyond),
		.join        = ending.relayed ? ending.eye.join * before * areas.light : 0,
		.connections = connectionSquares(ending, areas),
	};
}

}
