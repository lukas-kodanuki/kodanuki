#include "material/microfacet.h"

#include <algorithm>
#include <cmath>

namespace kodanuki {

double facetWidthOf(double roughness) {
	const double clamped = std::clamp(roughness, 0.0, 1.0);
	return std::max(clamped * clamped, 1e-3);
}

double facetDistribution(double cosine, double width) {
	if (cosine <= 0) {
		return 0;
	}
	const double squared = width * width;
	const double lower   = cosine * cosine * (squared - 1) + 1;
	return squared / (math::pi * lower * lower);
}

double smithVisibility(const Cosines& cosines, double width) {
	const double squared = width * width;
	const double first   = cosines.arriving * std::sqrt(squared + (1 - squared) * cosines.leaving * cosines.leaving);
	const double second  = cosines.leaving * std::sqrt(squared + (1 - squared) * cosines.arriving * cosines.arriving);
	const double total   = first + second;
	return total > 0 ? 0.5 / total : 0;
}

double smithMasking(double cosine, double width) {
	const double squared = width * width;
	const double under   = cosine + std::sqrt(squared + (1 - squared) * cosine * cosine);
	return under > 0 ? 2 * cosine / under : 0;
}

math::Vector halfwayOf(const math::Vector& outgoing, const math::Vector& incoming, const math::Vector& normal) {
	const math::Vector total = outgoing + incoming;
	const double size        = math::length(total);
	return size > 1e-12 ? total / size : normal;
}

math::Vector raisedFacet(const math::Vector& stretched, const math::UnitPair& pair) {
	const double flat         = stretched.x * stretched.x + stretched.y * stretched.y;
	const math::Vector first  = flat > 1e-12 ? math::Vector { -stretched.y, stretched.x, 0 } / std::sqrt(flat) : math::Vector { 1, 0, 0 };
	const math::Vector second = math::cross(stretched, first);
	const double reach        = std::sqrt(pair.u);
	const double angle        = 2 * math::pi * pair.v;
	const double across       = reach * std::cos(angle);
	const double lean         = 0.5 * (1 + stretched.z);
	const double along        = (1 - lean) * std::sqrt(std::max(1 - across * across, 0.0)) + lean * reach * std::sin(angle);
	const double height       = std::sqrt(std::max(1 - across * across - along * along, 0.0));
	return first * across + second * along + stretched * height;
}

math::Vector visibleFacet(const FacetDraw& draw) {
	const math::Basis basis        = math::basisAround(draw.normal);
	const math::Vector local       = math::toLocal(basis, draw.outgoing);
	const math::Vector squeezed    = { draw.width * local.x, draw.width * local.y, local.z };
	const math::Vector stretched   = math::normalized(squeezed);
	const math::Vector raised      = raisedFacet(stretched, draw.pair);
	const math::Vector unstretched = { draw.width * raised.x, draw.width * raised.y, std::max(raised.z, 1e-12) };
	return math::toWorld(basis, math::normalized(unstretched));
}

double visibleFacetDensity(const FacetQuery& query) {
	const double leaving = math::dot(query.normal, query.outgoing);
	if (leaving <= 0 || math::dot(query.normal, query.incoming) <= 0) {
		return 0;
	}
	const math::Vector facet = halfwayOf(query.outgoing, query.incoming, query.normal);
	if (math::dot(query.outgoing, facet) <= 0) {
		return 0;
	}
	return facetDistribution(math::dot(query.normal, facet), query.width) * smithMasking(leaving, query.width) / (4 * leaving);
}

double lobeSharpness(double width) {
	return std::min(2 * width, 1.0);
}

double dielectricReflectance(double cosine, double ratio) {
	const double leftover = 1 - ratio * ratio * (1 - cosine * cosine);
	if (leftover <= 0) {
		return 1;
	}
	const double onward        = std::sqrt(leftover);
	const double perpendicular = (ratio * cosine - onward) / (ratio * cosine + onward);
	const double parallel      = (ratio * onward - cosine) / (ratio * onward + cosine);
	return 0.5 * (perpendicular * perpendicular + parallel * parallel);
}

Refraction refractedThrough(const math::Vector& normal, const math::Vector& outgoing, double ratio) {
	const double cosine   = math::dot(normal, outgoing);
	const double leftover = 1 - ratio * ratio * (1 - cosine * cosine);
	if (leftover <= 0) {
		return Refraction {
			.passed    = false,
			.direction = { 0, 0, 0 },
		};
	}
	return Refraction {
		.passed    = true,
		.direction = (normal * cosine - outgoing) * ratio - normal * std::sqrt(leftover),
	};
}

double schlickChannel(double faceOn, double rest) {
	return faceOn + (1 - faceOn) * rest;
}

Colour schlickOf(const Colour& faceOn, double cosine) {
	const double rest = std::pow(std::clamp(1 - cosine, 0.0, 1.0), 5.0);
	return { schlickChannel(faceOn.red, rest), schlickChannel(faceOn.green, rest), schlickChannel(faceOn.blue, rest) };
}

Colour averagedFresnel(const Colour& faceOn) {
	return { schlickChannel(faceOn.red, 1.0 / 21), schlickChannel(faceOn.green, 1.0 / 21), schlickChannel(faceOn.blue, 1.0 / 21) };
}

}
