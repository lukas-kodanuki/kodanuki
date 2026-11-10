#include "transport/strategy.h"

#include "extras/strategy_correct_connect/connect.h"
#include "extras/strategy_correct_join/join.h"
#include "transport/trace.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace kodanuki {

void meetStep(const Tracing& tracing, const StrategyVisit& visit) {
	if (visit.moment == StrategyMoment::Arrival) {
		const Arrival arrival = {
			.path = visit.path,
			.hit  = visit.hit,
		};

		metAlong(tracing, arrival, visit.companions.rules);
		return;
	}
	metGlow(tracing, visit.path, visit.companions.rules);
}

void aimStep(const Tracing& tracing, const StrategyVisit& visit) {
	if (visit.moment != StrategyMoment::Vertex) {
		return;
	}

	const Standing standing = {
		.path       = visit.path,
		.companions = visit.companions,
		.counts     = aimCounts(tracing, visit.path, visit.path.depth),
	};

	aimedFrom(tracing, standing, visit.lines);
}

void strategyStep(StrategyMember member, const Tracing& tracing, const StrategyVisit& visit) {
	switch (member) {
		case StrategyMember::Meet: meetStep(tracing, visit); return;
		case StrategyMember::Aim: aimStep(tracing, visit); return;
		case StrategyMember::Join: return;
		case StrategyMember::Connect: connectStep(tracing, visit); return;
	}
}

LensLine noLensLine() {
	const Shadow shadow = {
		.origin    = { 0, 0, 0 },
		.direction = { 0, 0, 1 },
		.reach     = 0,
		.cell      = noCell,
		.within    = noMaterial,
		.medium    = noMaterial,
		.passage   = Passage::ToLens,
		.crossings = 0,
	};

	return LensLine {
		.found    = false,
		.shadow   = shadow,
		.radiance = flatSpectrum(0),
		.pixel    = 0,
	};
}

LensLine lensLineFrom(StrategyMember member, const Tracing& tracing, const LightStanding& standing) {
	switch (member) {
		case StrategyMember::Join: return joinedLine(tracing, standing);
		case StrategyMember::Meet:
		case StrategyMember::Aim:
		case StrategyMember::Connect: return noLensLine();
	}

	return noLensLine();
}

Lines noLines() {
	return Lines {
		.items = {},
		.count = 0,
	};
}

void addLine(Lines& lines, const Line& line) {
	if (lines.count >= mostLines) {
		std::fprintf(stderr, "kodanuki: a vertex asks for more than %u lines\n", mostLines);
		std::exit(1);
	}
	lines.items[lines.count] = line;
	lines.count += 1;
}

bool strategyOn(const Strategies& strategies, StrategyMember member) {
	return std::find(strategies.members.begin(), strategies.members.end(), member) != strategies.members.end();
}

bool strategyLeads(const Strategies& strategies) {
	return strategyOn(strategies, StrategyMember::Connect) && !strategies.lampShares.empty();
}

StrategyCounts aimCounts(const Tracing& tracing, const Path& path, int vertex) {
	const StrategyCounts counts = countsOf(tracing.strategies);
	return vertex == 0 ? sharedAim(counts, path.numbers.branches) : counts;
}

StrategyCounts countsOf(const Strategies& strategies) {
	return StrategyCounts {
		.aim     = strategyOn(strategies, StrategyMember::Aim) ? 1.0 : 0.0,
		.join    = strategyOn(strategies, StrategyMember::Join) ? strategies.joinShare : 0.0,
		.connect = strategyLeads(strategies) ? 1.0 : 0.0,
	};
}

Strategies coreStrategies() {
	return Strategies {
		.members    = { StrategyMember::Meet, StrategyMember::Aim },
		.joinShare  = 0.125,
		.lampShares = {},
	};
}

TransportLimits transportLimits(const Render& render) {
	return TransportLimits {
		.minimumDepth      = 4,
		.maximumDepth      = render.surfaces,
		.maximumCollisions = render.collisions,
		.bias              = 4e-3,
	};
}

DepthCaps depthCaps(const TransportLimits& limits) {
	return DepthCaps {
		.surfaces   = limits.maximumDepth,
		.collisions = limits.maximumCollisions,
	};
}

}
