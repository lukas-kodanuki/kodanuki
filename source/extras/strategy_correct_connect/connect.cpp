#include "extras/strategy_correct_connect/connect.h"

#include "spectral/spectrum.h"
#include "transport/trace.h"

#include <cmath>

namespace kodanuki {

struct Edge {
	math::Vector direction;
	double distance;
	double eyeCosine;
	double lightCosine;
};

struct EdgeResponses {
	Response eye;
	Response light;
};

struct WeighedEdge {
	const Connection& connection;
	const Edge& edge;
	const EdgeResponses& responses;
};

Line noConnectionLine() {
	const Shadow shadow = {
		.origin    = { 0, 0, 0 },
		.direction = { 0, 0, 1 },
		.reach     = 0,
		.cell      = noCell,
		.within    = noMaterial,
		.medium    = noMaterial,
		.passage   = Passage::Between,
		.crossings = 0,
	};

	return Line {
		.shadow   = shadow,
		.radiance = flatSpectrum(0),
	};
}

Edge edgeOf(const Connection& connection) {
	const math::Vector offset    = connection.light.vertex.point - connection.path.vertex.point;
	const double distance        = math::length(offset);
	const math::Vector direction = offset * (1 / distance);
	return Edge {
		.direction   = direction,
		.distance    = distance,
		.eyeCosine   = math::dot(connection.path.vertex.surface, direction),
		.lightCosine = -math::dot(connection.light.vertex.surface, direction),
	};
}

VertexCounts connectedCounts(const Connection& connection) {
	const VertexCounts eye = standingCounts(connection.path);
	return VertexCounts {
		.surfaces   = eye.surfaces + connection.light.depth + 1,
		.collisions = eye.collisions,
	};
}

bool connectsTo(const Tracing& tracing, const Connection& connection, const Edge& edge) {
	const bool fits    = connectedCounts(connection).surfaces <= tracing.limits.maximumDepth;
	const bool facing  = edge.eyeCosine > 0 && edge.lightCosine > 0;
	const bool arrived = std::abs(math::dot(connection.light.arrival, connection.light.vertex.surface)) > 0;
	return connection.light.connectable && fits && facing && arrived && edge.distance > connection.path.vertex.lift + connection.light.vertex.lift;
}

EdgeResponses edgeResponses(const Tracing& tracing, const Connection& connection, const Edge& edge) {
	const Path& path = connection.path;

	const BsdfPair eye = {
		.outgoing    = -path.heading,
		.incoming    = edge.direction,
		.wavelengths = path.wavelengths,
	};

	const BsdfPair far = {
		.outgoing    = -edge.direction,
		.incoming    = connection.light.arrival,
		.wavelengths = path.wavelengths,
	};

	return EdgeResponses {
		.eye   = respondBsdf(tracing.materials, path.vertex.bsdf, eye),
		.light = respondBsdf(tracing.materials, connection.light.vertex.bsdf, far),
	};
}

double eyeForwardDensity(const Tracing& tracing, const WeighedEdge& weighed) {
	const Standing standing = {
		.path       = weighed.connection.path,
		.companions = weighed.connection.companions,
		.counts     = countsOf(tracing.strategies),
	};

	const OwnDensity toward = {
		.incoming = weighed.edge.direction,
		.density  = weighed.responses.eye.density,
	};

	return weighedDensity(tracing, standing, toward);
}

double lightDrawnDensity(const Tracing& tracing, const Connection& connection, const Edge& edge) {
	const BsdfPair pair = {
		.outgoing    = connection.light.arrival,
		.incoming    = -edge.direction,
		.wavelengths = connection.path.wavelengths,
	};

	return bsdfDensity(tracing.materials, connection.light.vertex.bsdf, pair);
}

double eyeBackDensity(const Tracing& tracing, const Connection& connection, const Edge& edge) {
	const TurnBack turn = {
		.bsdf        = connection.path.vertex.bsdf,
		.heading     = connection.path.heading,
		.drawn       = edge.direction,
		.wavelengths = connection.path.wavelengths,
	};

	return densityBack(tracing.materials, turn);
}

double connectionWeight(const Tracing& tracing, const WeighedEdge& weighed) {
	const Connection& connection = weighed.connection;
	const Edge& edge             = weighed.edge;
	const StrategyCounts counts  = countsOf(tracing.strategies);

	const ConnectEnding ending = {
		.eye          = connection.path.weighing,
		.light        = connection.light.weighing,
		.eyeForward   = eyeForwardDensity(tracing, weighed),
		.eyeReverse   = eyeBackDensity(tracing, connection, edge),
		.lightForward = lightDrawnDensity(tracing, connection, edge),
		.lightReverse = weighed.responses.light.density,
		.eyeCosine    = edge.eyeCosine,
		.lightCosine  = edge.lightCosine,
		.distance     = edge.distance,
		.relayed      = connection.light.relayed,
		.capped       = atDepthCap(connectedCounts(connection), depthCaps(tracing.limits)),
		.counts       = counts,
	};

	const Densities densities = connectDensities(ending);
	return weightOf(densities, counts.connect);
}

double edgeFactor(const Connection& connection, const Edge& edge) {
	const LightVertex& light = connection.light;
	const double eyeShading  = std::abs(math::dot(connection.path.vertex.bsdf.normal, edge.direction));
	const double lit         = std::abs(math::dot(light.arrival, light.vertex.bsdf.normal));
	const double slant       = std::abs(math::dot(light.arrival, light.vertex.surface));
	const double both        = connection.path.dispersed && light.dispersed ? 1.0 / spectrumLanes : 1.0;
	return eyeShading * lit * edge.lightCosine / (slant * edge.distance * edge.distance) * both;
}

Shadow connectionShadow(const Connection& connection) {
	const math::Vector origin = connection.path.vertex.point + connection.path.vertex.surface * connection.path.vertex.lift;
	const math::Vector end    = connection.light.vertex.point + connection.light.vertex.surface * connection.light.vertex.lift;
	const math::Vector offset = end - origin;
	const double reach        = math::length(offset);
	return Shadow {
		.origin    = origin,
		.direction = offset * (1 / reach),
		.reach     = reach,
		.cell      = noCell,
		.within    = noMaterial,
		.medium    = noMaterial,
		.passage   = Passage::Between,
		.crossings = 0,
	};
}

bool connectionEnds(const Tracing& tracing, const Connection& connection) {
	const RuleEnding ending = {
		.light    = connection.lamp,
		.bits     = connectedBits(connection.companions.rules.bits, connection.light.rules),
		.standing = connection.light.rules.standing,
	};

	return lightEnds(tracing.features, ending);
}

Line connectionLine(const Tracing& tracing, const Connection& connection) {
	const Edge edge = edgeOf(connection);
	if (!connectsTo(tracing, connection, edge) || !connectionEnds(tracing, connection)) {
		return noConnectionLine();
	}
	const EdgeResponses responses = edgeResponses(tracing, connection, edge);
	const Spectrum4 value         = connection.path.throughput * responses.eye.value * responses.light.value * connection.light.throughput;
	if (!(laneMaximum(value) > 0)) {
		return noConnectionLine();
	}

	const WeighedEdge weighed = {
		.connection = connection,
		.edge       = edge,
		.responses  = responses,
	};

	return Line {
		.shadow   = connectionShadow(connection),
		.radiance = value * (edgeFactor(connection, edge) * connectionWeight(tracing, weighed)),
	};
}

bool connectsFrom(const Tracing& tracing, const Path& path) {
	return path.medium == noMaterial && !isExact(tracing.materials, path.vertex.bsdf.material);
}

void connectStep(const Tracing& tracing, const StrategyVisit& visit) {
	if (visit.moment != StrategyMoment::Vertex || !connectsFrom(tracing, visit.path)) {
		return;
	}
	for (std::uint32_t k = 0; k < visit.lead.count; ++k) {
		const Connection connection = {
			.path       = visit.path,
			.companions = visit.companions,
			.light      = visit.lead.items[k],
			.lamp       = visit.lead.lamp,
		};

		const Line line = connectionLine(tracing, connection);
		if (laneMaximum(line.radiance) > 0) {
			addLine(visit.lines, line);
		}
	}
}

}
