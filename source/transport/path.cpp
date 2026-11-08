#include "transport/path.h"

#include <limits>

namespace kodanuki {

constexpr int firstEvent      = 4;
constexpr int eventDimensions = 8;
constexpr int plainFirst      = 1 << 20;
constexpr int plainDimensions = 5;
constexpr int lightFirst      = 1 << 21;
constexpr int lightDimensions = 3;
constexpr int collisionFirst  = 1 << 22;

Coordinate coordinateOf(int event, Draw draw) {
	return Coordinate {
		.event = event,
		.draw  = draw,
	};
}

std::uint64_t dimensionOf(const Coordinate& coordinate) {
	const int role = int(coordinate.draw);
	if (coordinate.draw < Draw::LightU) {
		return std::uint64_t(role);
	}
	if (coordinate.draw == Draw::Collision) {
		return std::uint64_t(collisionFirst + coordinate.event);
	}
	if (coordinate.draw >= Draw::LightHue) {
		return std::uint64_t(lightFirst + coordinate.event * lightDimensions + role - int(Draw::LightHue));
	}
	if (coordinate.draw >= Draw::MixturePart) {
		return std::uint64_t(plainFirst + coordinate.event * plainDimensions + role - int(Draw::MixturePart));
	}
	return std::uint64_t(firstEvent + coordinate.event * eventDimensions + role - int(Draw::LightU));
}

Ray leavingRay(const Vertex& vertex, const math::Vector& direction, std::uint32_t medium) {
	const double side = math::dot(direction, vertex.surface) > 0 ? vertex.lift : -vertex.lift;
	return Ray {
		.origin    = vertex.point + vertex.surface * side,
		.direction = direction,
		.far       = std::numeric_limits<double>::infinity(),
		.inside    = medium != noMaterial,
		.within    = medium,
	};
}

Vertex blankVertex() {
	const Sides sides = {
		.travelling = flatSpectrum(1),
		.beyond     = flatSpectrum(1),
		.parted     = false,
		.inside     = false,
	};

	const Bsdf bsdf = {
		.material = noMaterial,
		.albedo   = { 0, 0, 0 },
		.normal   = { 0, 0, 1 },
		.sides    = sides,
	};

	return Vertex {
		.point    = { 0, 0, 0 },
		.surface  = { 0, 0, 1 },
		.bsdf     = bsdf,
		.glow     = { 0, 0, 0 },
		.lift     = 0,
		.leaf     = noLeaf,
		.cell     = noLeaf,
		.sharp    = false,
		.collided = false,
	};
}

Path freshPath(const PathNumbers& numbers, const Wavelengths& wavelengths) {
	const Weighing weighing = {
		.join        = 0,
		.connections = 0,
		.cut         = 0,
		.drawn       = 0,
		.relays      = false,
		.aimed       = Aimed::Camera,
	};

	return Path {
		.wavelengths = wavelengths,
		.throughput  = flatSpectrum(1),
		.radiance    = flatSpectrum(0),
		.exact       = true,
		.dispersed   = false,
		.vertex      = blankVertex(),
		.heading     = { 0, 0, 1 },
		.drawnAt     = { 0, 0, 0 },
		.legFrom     = { 0, 0, 0 },
		.crossings   = 0,
		.sharpness   = 0,
		.density     = 0,
		.depth       = 0,
		.collisions  = 0,
		.medium      = noMaterial,
		.weighing    = weighing,
		.numbers     = numbers,
	};
}

VertexCounts standingCounts(const Path& path) {
	return VertexCounts {
		.surfaces   = path.depth + 1 - path.collisions,
		.collisions = path.collisions,
	};
}

}
