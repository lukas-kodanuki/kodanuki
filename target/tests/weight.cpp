#include "transport/weight.h"
#include "check.h"
#include "math/vector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace kodanuki {

struct Corner {
	math::Vector point;
	math::Vector normal;
	double forward;
	double reverse;
	bool sharp;
	bool exact;
	bool connectable;
	bool collided;
};

struct Chain {
	std::vector<Corner> corners;
	double lens;
	double area;
	double direction;
	double aim;
	bool singular;
	bool meetless;
	bool reached;
	VertexCounts stood;
	DepthCaps caps;
	StrategyCounts counts;
};

bool cappedChain(const Chain& chain) {
	return atDepthCap(chain.stood, chain.caps);
}

double cosineAt(const Chain& chain, int at, int toward) {
	if (chain.corners[std::size_t(at)].collided) {
		return 1;
	}
	const math::Vector offset = chain.corners[std::size_t(toward)].point - chain.corners[std::size_t(at)].point;
	return std::abs(math::dot(chain.corners[std::size_t(at)].normal, math::normalized(offset)));
}

double squaredGap(const Chain& chain, int a, int b) {
	const math::Vector offset = chain.corners[std::size_t(b)].point - chain.corners[std::size_t(a)].point;
	return math::dot(offset, offset);
}

int lastOf(const Chain& chain) {
	return int(chain.corners.size()) - 1;
}

double eyeArea(const Chain& chain, int i) {
	const double toward = i == 1 ? chain.lens : chain.corners[std::size_t(i - 1)].forward;
	return toward * cosineAt(chain, i, i - 1) / squaredGap(chain, i - 1, i);
}

double lightArea(const Chain& chain, int i) {
	const int k = lastOf(chain);
	if (chain.corners[std::size_t(i)].collided) {
		return 0;
	}
	if (i == k) {
		return chain.area;
	}
	const double toward = i == k - 1 ? chain.direction : chain.corners[std::size_t(i + 1)].reverse;
	return toward * cosineAt(chain, i, i + 1) / squaredGap(chain, i, i + 1);
}

double strategyDensity(const Chain& chain, int t) {
	const int k   = lastOf(chain);
	double result = 1;
	for (int i = 1; i < t; ++i) {
		result *= eyeArea(chain, i);
	}
	for (int i = t; i <= k; ++i) {
		result *= lightArea(chain, i);
	}
	return result;
}

double meetDensity(const Chain& chain) {
	return chain.singular || chain.meetless ? 0 : strategyDensity(chain, lastOf(chain) + 1);
}

double aimStrategyDensity(const Chain& chain) {
	const int k = lastOf(chain);
	if (chain.corners[std::size_t(k - 1)].exact) {
		return 0;
	}
	const double point = chain.singular ? 1 : chain.aim * cosineAt(chain, k, k - 1) / squaredGap(chain, k - 1, k);
	return chain.counts.aim * strategyDensity(chain, k) / chain.area * point;
}

double joinStrategyDensity(const Chain& chain) {
	const int k        = lastOf(chain);
	const bool allowed = k >= 2 && chain.corners[std::size_t(k - 1)].sharp && !chain.corners[1].sharp;
	return allowed ? chain.counts.join * strategyDensity(chain, 1) : 0;
}

struct BruteWeights {
	double meet;
	double aim;
	double join;
	std::vector<double> connections;
};

double connectStrategyDensity(const Chain& chain, int t) {
	const bool open = chain.corners[std::size_t(t - 1)].connectable && chain.corners[std::size_t(t)].connectable;
	return open ? chain.counts.connect * strategyDensity(chain, t) : 0;
}

BruteWeights bruteWeights(const Chain& chain) {
	const int k        = lastOf(chain);
	const double meet  = meetDensity(chain);
	const double aim   = aimStrategyDensity(chain);
	const double join  = joinStrategyDensity(chain);
	double connections = 0;
	for (int t = 2; t <= k - 1; ++t) {
		const double one = connectStrategyDensity(chain, t);
		connections += one * one;
	}
	const double total = meet * meet + aim * aim + join * join + connections;

	BruteWeights weights = {
		.meet        = meet * meet / total,
		.aim         = aim * aim / total,
		.join        = join * join / total,
		.connections = {},
	};

	for (int t = 2; t <= k - 1; ++t) {
		const double one = connectStrategyDensity(chain, t);
		weights.connections.push_back(one * one / total);
	}
	return weights;
}

double emissionTowardEye(const Chain& chain) {
	const int k = lastOf(chain);
	return chain.singular ? chain.area * chain.direction / squaredGap(chain, k - 1, k) : chain.area * chain.direction / cosineAt(chain, k, k - 1);
}

Weighing eyeWeighingAt(const Chain& chain, int last) {
	Weighing weighing = weighingAtLens(chain.counts, chain.lens);
	for (int i = 1; i <= last; ++i) {
		const HitFactors hit = {
			.cosine      = cosineAt(chain, i, i - 1),
			.distance    = std::sqrt(squaredGap(chain, i - 1, i)),
			.joinable    = !chain.corners[std::size_t(i)].sharp && !chain.corners[std::size_t(i)].collided,
			.connectable = chain.corners[std::size_t(i)].connectable,
		};

		weighing = weighingAtHit(weighing, hit);
		if (i == last) {
			break;
		}

		const ScatterFactors scatter = {
			.forward     = chain.corners[std::size_t(i)].forward,
			.reverse     = chain.corners[std::size_t(i)].reverse,
			.cosine      = cosineAt(chain, i, i + 1),
			.exact       = chain.corners[std::size_t(i)].exact,
			.sharp       = chain.corners[std::size_t(i)].sharp,
			.connectable = chain.corners[std::size_t(i)].connectable,
			.aimed       = Aimed::Every,
		};

		weighing = weighingAfterScatter(weighing, scatter);
	}
	return weighing;
}

double recursiveAimWeight(const Chain& chain) {
	const int k             = lastOf(chain);
	const Weighing weighing = eyeWeighingAt(chain, k - 1);
	const Corner& corner    = chain.corners[std::size_t(k - 1)];

	const AimEnding ending = {
		.weighing = weighing,
		.forward  = corner.forward,
		.reverse  = corner.reverse,
		.aim      = chain.singular ? 0 : chain.aim,
		.emission = emissionTowardEye(chain),
		.cosine   = cosineAt(chain, k - 1, k),
		.sharp    = corner.sharp,
		.capped   = cappedChain(chain),
		.counts   = chain.counts,
	};

	const Densities densities = aimDensities(ending);
	return weightOf(densities, densities.aim);
}

double recursiveMeetWeight(const Chain& chain) {
	const int k             = lastOf(chain);
	const Weighing standing = eyeWeighingAt(chain, k - 1);
	const Corner& corner    = chain.corners[std::size_t(k - 1)];

	const ScatterFactors scatter = {
		.forward     = corner.forward,
		.reverse     = corner.reverse,
		.cosine      = cosineAt(chain, k - 1, k),
		.exact       = corner.exact,
		.sharp       = corner.sharp,
		.connectable = corner.connectable,
		.aimed       = corner.exact ? Aimed::None : Aimed::Every,
	};

	const Weighing weighing = weighingAfterScatter(standing, scatter);

	const MeetEnding ending = {
		.weighing = weighing,
		.aim      = corner.exact ? 0 : chain.aim,
		.emission = emissionTowardEye(chain),
		.counts   = chain.counts,
	};

	const Densities densities = meetDensities(ending);
	return weightOf(densities, densities.meet);
}

LightWeighing lightWeighingAt(const Chain& chain, int last) {
	const int k = lastOf(chain);

	const LampStart start = {
		.emission = chain.area * chain.direction,
		.cosine   = chain.singular ? 1 : cosineAt(chain, k, k - 1),
		.singular = chain.singular,
	};

	LightWeighing weighing = weighingAtLamp(start, chain.counts);
	for (int j = k - 1; j >= 1; --j) {
		const double gap = squaredGap(chain, j, j + 1);

		const LightHitFactors hit = {
			.cosine      = cosineAt(chain, j, j + 1),
			.distance    = std::sqrt(gap),
			.aim         = chain.corners[std::size_t(j)].exact ? 0 : chain.singular ? gap
																					: chain.aim,
			.connectable = chain.corners[std::size_t(j)].connectable,
		};

		weighing = lightWeighingAtHit(weighing, hit);
		if (j == last) {
			break;
		}

		const ScatterFactors scatter = {
			.forward     = chain.corners[std::size_t(j)].reverse,
			.reverse     = chain.corners[std::size_t(j)].forward,
			.cosine      = cosineAt(chain, j, j - 1),
			.exact       = chain.corners[std::size_t(j)].exact,
			.sharp       = chain.corners[std::size_t(j)].sharp,
			.connectable = chain.corners[std::size_t(j)].connectable,
			.aimed       = Aimed::None,
		};

		weighing = lightWeighingAfterScatter(weighing, scatter);
	}
	return weighing;
}

double recursiveJoinWeight(const Chain& chain) {
	const LightWeighing weighing = lightWeighingAt(chain, 1);

	const JoinEnding ending = {
		.weighing = weighing,
		.forward  = chain.corners[1].forward,
		.lens     = eyeArea(chain, 1),
		.capped   = cappedChain(chain),
		.counts   = chain.counts,
	};

	const Densities densities = joinDensities(ending);
	return weightOf(densities, densities.join);
}

double recursiveConnectWeight(const Chain& chain, int t) {
	const int a               = t - 1;
	const Weighing eye        = eyeWeighingAt(chain, a);
	const LightWeighing light = lightWeighingAt(chain, t);
	const Corner& eyeCorner   = chain.corners[std::size_t(a)];
	const Corner& lightCorner = chain.corners[std::size_t(t)];
	const int k               = lastOf(chain);

	const ConnectEnding ending = {
		.eye          = eye,
		.light        = light,
		.eyeForward   = eyeCorner.forward,
		.eyeReverse   = eyeCorner.reverse,
		.lightForward = lightCorner.reverse,
		.lightReverse = lightCorner.forward,
		.eyeCosine    = cosineAt(chain, a, t),
		.lightCosine  = cosineAt(chain, t, a),
		.distance     = std::sqrt(squaredGap(chain, a, t)),
		.relayed      = chain.corners[std::size_t(k - 1)].sharp,
		.capped       = cappedChain(chain),
		.counts       = chain.counts,
	};

	const Densities densities = connectDensities(ending);
	return weightOf(densities, chain.counts.connect);
}

struct ChainShape {
	int k;
	bool singular;
	int exact;
	int collided;
	int run;
};

VertexCounts countsUpTo(const std::vector<Corner>& corners, int last) {
	VertexCounts counts = {
		.surfaces   = 0,
		.collisions = 0,
	};

	for (int i = 1; i <= last; ++i) {
		const bool collided = corners[std::size_t(i)].collided;
		counts.surfaces += collided ? 0 : 1;
		counts.collisions += collided ? 1 : 0;
	}
	return counts;
}

bool scattersPast(const std::vector<Corner>& corners, const DepthCaps& caps, int last) {
	for (int i = 1; i <= last; ++i) {
		const VertexCounts counts = countsUpTo(corners, i);
		if (counts.surfaces >= caps.surfaces || counts.collisions >= caps.collisions) {
			return false;
		}
	}
	return true;
}

DepthCaps randomCaps(std::mt19937& random, const VertexCounts& stood) {
	std::uniform_int_distribution<int> slack(0, 2);
	return DepthCaps {
		.surfaces   = stood.surfaces + slack(random),
		.collisions = std::max(1, stood.collisions + slack(random) - 1),
	};
}

Chain randomChain(std::mt19937& random, const ChainShape& shape) {
	std::uniform_real_distribution<double> unit(0, 1);
	std::uniform_real_distribution<double> spread(-1, 1);
	std::vector<Corner> corners;
	for (int i = 0; i <= shape.k; ++i) {
		const math::Vector point  = { spread(random) * 2, spread(random) * 2, double(i) + spread(random) * 0.3 };
		const math::Vector normal = { spread(random), spread(random), 1 + unit(random) };
		const double forward      = 0.05 + 3 * unit(random);
		const double reverse      = 0.05 + 3 * unit(random);
		const bool sharp          = unit(random) < 0.5;
		const bool collided       = shape.collided >= 0 && i >= shape.collided && i < std::min(shape.collided + shape.run, shape.k);
		const bool exact          = i == shape.exact && !collided;
		const bool open           = unit(random) < 0.75;

		const Corner corner = {
			.point       = point,
			.normal      = math::normalized(normal),
			.forward     = exact ? 1 : forward,
			.reverse     = exact ? 1 : (collided ? 0 : reverse),
			.sharp       = (sharp || exact) && !collided,
			.exact       = exact,
			.connectable = open && !exact && !collided,
			.collided    = collided,
		};

		corners.push_back(corner);
	}

	const StrategyCounts counts = {
		.aim     = 1,
		.join    = 0.125 + unit(random),
		.connect = 1,
	};

	const VertexCounts stood = countsUpTo(corners, shape.k - 1);
	const DepthCaps caps     = randomCaps(random, stood);
	return Chain {
		.corners   = corners,
		.lens      = 0.2 + unit(random),
		.area      = shape.singular ? 0.3 + unit(random) : 0.1 + unit(random),
		.direction = 0.1 + unit(random),
		.aim       = 0.1 + 2 * unit(random),
		.singular  = shape.singular,
		.meetless  = !scattersPast(corners, caps, shape.k - 1),
		.reached   = scattersPast(corners, caps, shape.k - 2),
		.stood     = stood,
		.caps      = caps,
		.counts    = counts,
	};
}

ChainShape chainShape(std::mt19937& random, int s) {
	const int k = 2 + s % 5;
	std::uniform_int_distribution<int> inner(1, k - 1);
	return ChainShape {
		.k        = k,
		.singular = s % 7 == 0,
		.exact    = s % 3 == 0 ? inner(random) : -1,
		.collided = s % 5 == 2 || s % 5 == 4 ? inner(random) : -1,
		.run      = 1 + s % 3,
	};
}

struct ChainGap {
	double gap;
	bool weighed;
};

ChainGap chainGap(const Chain& chain) {
	const int k              = lastOf(chain);
	const BruteWeights brute = bruteWeights(chain);
	if (!chain.reached || !(brute.meet + brute.aim + brute.join >= 0)) {
		return ChainGap {
			.gap     = 0,
			.weighed = false,
		};
	}
	const double meet       = chain.singular || cappedChain(chain) ? 0 : recursiveMeetWeight(chain);
	const double aim        = chain.corners[std::size_t(k - 1)].exact ? 0 : recursiveAimWeight(chain);
	const double join       = recursiveJoinWeight(chain);
	const double joinWanted = joinStrategyDensity(chain) > 0 ? brute.join : join;
	double gap              = std::max({ std::abs(meet - brute.meet), std::abs(aim - brute.aim), std::abs(join - joinWanted) });
	double sum              = meet + aim + (joinStrategyDensity(chain) > 0 ? join : 0);
	for (int t = 2; t <= k - 1; ++t) {
		const bool made        = connectStrategyDensity(chain, t) > 0;
		const double connected = made ? recursiveConnectWeight(chain, t) : 0;
		gap                    = std::max(gap, std::abs(connected - brute.connections[std::size_t(t - 2)]));
		sum += connected;
	}
	return ChainGap {
		.gap     = std::max(gap, std::abs(sum - 1)),
		.weighed = true,
	};
}

void checkWeight() {
	std::mt19937 random(17);
	double worst     = 0;
	double joined    = 0;
	double connected = 0;
	int exact        = 0;
	int capped       = 0;
	int collided     = 0;
	int collisionCap = 0;
	for (int s = 0; s < 6000; ++s) {
		const ChainShape shape   = chainShape(random, s);
		const Chain chain        = randomChain(random, shape);
		const ChainGap gap       = chainGap(chain);
		worst                    = std::max(worst, gap.gap);
		const BruteWeights brute = bruteWeights(chain);
		joined += brute.join > 0.01 ? 1 : 0;
		connected += std::any_of(brute.connections.begin(), brute.connections.end(), [](double weight) {
			return weight > 0.01;
		})

						 ? 1
						 : 0;
		exact += gap.weighed && shape.exact >= 0 ? 1 : 0;
		capped += gap.weighed && chain.meetless ? 1 : 0;
		collided += gap.weighed && shape.collided >= 0 ? 1 : 0;
		collisionCap += gap.weighed && chain.stood.collisions >= chain.caps.collisions && chain.stood.surfaces < chain.caps.surfaces ? 1 : 0;
	}
	std::printf("weight: %d chains with a join weight over 1%%, %d with a connection weight over 1%%, %d with an exact vertex, %d at a depth cap, %d with a collision, %d at the collision cap alone; worst gap between the recursive and the brute-force weights, or of their sum from one, %.2e\n", int(joined), int(connected), exact, capped, collided, collisionCap, worst);
	check(joined > 100, "the brute-force test reaches paths that join weighs");
	check(connected > 1000, "the brute-force test reaches paths that a connection weighs");
	check(exact > 1000, "the brute-force test reaches paths through an exact vertex");
	check(capped > 1000, "the brute-force test reaches paths at a depth cap, which meet cannot make");
	check(collisionCap > 300, "the brute-force test reaches paths at the collision cap under the surface cap, which meet cannot make either");
	check(collided > 1000, "the brute-force test reaches paths through a collision, aimed from as from a surface, which no light walk makes");
	check(worst < 1e-9, "the recursive weights of meet, aim, join and every connection equal a brute-force power heuristic over every strategy and sum to one, through exact, unconnectable and collision vertices too");
}

}
