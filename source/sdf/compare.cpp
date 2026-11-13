#include "sdf/compare.h"

#include <algorithm>
#include <vector>

namespace kodanuki {

std::vector<double> leafNumbers(const Leaf& leaf) {
	const Placement& place      = leaf.placement;
	std::vector<double> numbers = { double(leaf.program), double(leaf.arguments), double(leaf.material), double(leaf.rules), place.stretch };
	numbers.insert(numbers.end(), &place.linear[0][0], &place.linear[0][0] + 9);
	numbers.insert(numbers.end(), place.origin, place.origin + 3);
	numbers.insert(numbers.end(), leaf.coats.begin(), leaf.coats.end());
	return numbers;
}

bool sameLeaf(const Leaf& a, const Leaf& b) {
	return a.groups == b.groups && leafNumbers(a) == leafNumbers(b);
}

bool sameNode(const Node& a, const Node& b) {
	return a.join == b.join && a.radius == b.radius && a.leaf == b.leaf && a.children == b.children;
}

bool samePrograms(const std::vector<Program>& a, const std::vector<Program>& b) {
	return std::equal(a.begin(), a.end(), b.begin(), b.end(), sameProgram);
}

bool sameShapes(const Scene& a, const Scene& b) {
	const bool leaves = std::equal(a.leaves.begin(), a.leaves.end(), b.leaves.begin(), b.leaves.end(), sameLeaf);
	const bool nodes  = std::equal(a.nodes.begin(), a.nodes.end(), b.nodes.begin(), b.nodes.end(), sameNode);
	const bool held   = a.arguments == b.arguments && a.objects == b.objects && leaves && nodes;
	return held && samePrograms(a.programs, b.programs) && samePrograms(a.objectPrograms, b.objectPrograms);
}

std::vector<double> colourNumbers(const Colour& colour) {
	return { colour.red, colour.green, colour.blue };
}

std::vector<double> vectorNumbers(const math::Vector& vector) {
	return { vector.x, vector.y, vector.z };
}

std::vector<double> joinedNumbers(const std::vector<std::vector<double>>& parts) {
	std::vector<double> numbers;
	for (const std::vector<double>& part : parts) {
		numbers.insert(numbers.end(), part.begin(), part.end());
	}
	return numbers;
}

std::vector<double> albedoNumbers(const Albedo& albedo) {
	const std::vector<double> stops = { albedo.stops.begin(), albedo.stops.end() };
	return joinedNumbers({ { double(albedo.program), double(albedo.arguments), double(albedo.world), double(stops.size()) }, stops });
}

std::vector<double> materialNumbers(const Material& material) {
	const std::vector<double> fields = { double(material.kind), material.roughness, material.refraction, material.dispersion, material.scatter, material.lean, material.strength, material.film[0], material.film[1], double(material.guided), double(material.relays), double(material.aimed) };
	return joinedNumbers({ fields, albedoNumbers(material.albedo), colourNumbers(material.absorb), colourNumbers(material.glow) });
}

bool sameMaterial(const Material& a, const Material& b) {
	return materialNumbers(a) == materialNumbers(b);
}

std::vector<double> coatNumbers(const Coat& coat) {
	const std::vector<double> fields = { double(coat.mask), double(coat.effect), coat.level, coat.width, coat.soft, double(coat.material), coat.amount, coat.ragged, coat.grain, double(coat.relief), coat.span };
	return joinedNumbers({ fields, vectorNumbers(coat.axis), vectorNumbers(coat.along), colourNumbers(coat.colour), albedoNumbers(coat.pattern), albedoNumbers(coat.height) });
}

bool sameCoat(const Coat& a, const Coat& b) {
	return coatNumbers(a) == coatNumbers(b);
}

bool sameLooks(const Looks& a, const Looks& b) {
	const bool materials = std::equal(a.materials.begin(), a.materials.end(), b.materials.begin(), b.materials.end(), sameMaterial);
	const bool coats     = std::equal(a.coats.begin(), a.coats.end(), b.coats.begin(), b.coats.end(), sameCoat);
	return materials && coats && a.groups == b.groups;
}

std::vector<double> lightNumbers(const Light& light) {
	const std::vector<double> fields = { double(light.kind), light.radius, light.angle, double(light.caustics) };
	return joinedNumbers({ fields, vectorNumbers(light.at), vectorNumbers(light.direction), vectorNumbers(light.along), vectorNumbers(light.across), colourNumbers(light.power) });
}

bool sameLight(const Light& a, const Light& b) {
	return a.reaches == b.reaches && lightNumbers(a) == lightNumbers(b);
}

std::vector<double> stopsNumbers(const std::vector<SkyStop>& stops) {
	std::vector<double> numbers = { double(stops.size()) };
	for (const SkyStop& stop : stops) {
		numbers.push_back(stop.height);
		const std::vector<double> colour = colourNumbers(stop.colour);
		numbers.insert(numbers.end(), colour.begin(), colour.end());
	}
	return numbers;
}

std::vector<double> skyNumbers(const Sky& sky) {
	const std::vector<double> fields  = { double(sky.kind), sky.haze, sky.strength, sky.spread };
	const std::vector<double> colours = joinedNumbers({ colourNumbers(sky.zenith), colourNumbers(sky.horizon), colourNumbers(sky.ground), colourNumbers(sky.glow) });
	return joinedNumbers({ fields, colours, stopsNumbers(sky.painted.day), stopsNumbers(sky.painted.dusk), stopsNumbers(sky.painted.night) });
}

std::vector<double> cloudsNumbers(const Clouds& clouds) {
	return { double(clouds.present), clouds.height, clouds.thickness, clouds.cover, clouds.density, clouds.span };
}

std::vector<double> starsNumbers(const Stars& stars) {
	return joinedNumbers({ { double(stars.present), stars.count, stars.strength, stars.angle }, vectorNumbers(stars.axis) });
}

bool sameLighting(const Lighting& a, const Lighting& b) {
	const bool lights = std::equal(a.lights.begin(), a.lights.end(), b.lights.begin(), b.lights.end(), sameLight);
	const bool sky    = skyNumbers(a.sky) == skyNumbers(b.sky);
	return lights && sky && cloudsNumbers(a.clouds) == cloudsNumbers(b.clouds) && starsNumbers(a.stars) == starsNumbers(b.stars);
}

std::vector<double> renderNumbers(const Render& render) {
	return { double(render.width), double(render.height), render.tolerance, double(render.seed), render.far, double(render.samples), double(render.batch), double(render.split), double(render.surfaces), double(render.collisions) };
}

bool sameSettings(const Shot& a, const Shot& b) {
	const bool clip = a.clip.duration == b.clip.duration && a.clip.rate == b.clip.rate && a.clip.cuts == b.clip.cuts;
	return clip && renderNumbers(a.render) == renderNumbers(b.render);
}

}
