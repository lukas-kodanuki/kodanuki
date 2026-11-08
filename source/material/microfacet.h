#pragma once

#include "math/random.h"
#include "math/vector.h"
#include "spectral/colour.h"

namespace kodanuki {

/**
 * The cosines of a pair of directions at a surface.
 */
struct Cosines {
	/**
	 * The cosine toward where the light leaves to.
	 */
	double leaving;
	/**
	 * The cosine toward where the light arrives from.
	 */
	double arriving;
};

/**
 * A facet to draw, seen from one direction.
 */
struct FacetDraw {
	/**
	 * The unit surface normal.
	 */
	math::Vector normal;
	/**
	 * The unit direction the facet is seen from.
	 */
	math::Vector outgoing;
	/**
	 * The GGX alpha.
	 */
	double width;
	/**
	 * The two numbers the facet is drawn with.
	 */
	math::UnitPair pair;
};

/**
 * A pair of directions whose facet density is asked.
 */
struct FacetQuery {
	/**
	 * The unit surface normal.
	 */
	math::Vector normal;
	/**
	 * The unit direction the facet is seen from.
	 */
	math::Vector outgoing;
	/**
	 * The unit direction reflected off the facet.
	 */
	math::Vector incoming;
	/**
	 * The GGX alpha.
	 */
	double width;
};

/**
 * A direction bent through a boundary, or the lack of one.
 */
struct Refraction {
	/**
	 * Whether any light gets through. It is false past the critical
	 * angle.
	 */
	bool passed;
	/**
	 * The unit direction that carries on, when it passes.
	 */
	math::Vector direction;
};

/**
 * Turn a roughness into a GGX alpha.
 *
 * The roughness is clamped to [0, 1], squared, and floored at a
 * thousandth.
 *
 * @param roughness The roughness.
 */
double facetWidthOf(double roughness);

/**
 * Return the GGX distribution of facet normals.
 *
 * It is zero for a facet that faces away.
 *
 * @param cosine The cosine between the surface normal and the facet
 *        normal.
 * @param width The GGX alpha.
 */
double facetDistribution(double cosine, double width);

/**
 * Return the height-correlated Smith visibility, already divided by the
 * four cosines of the reflection integral.
 *
 * @param cosines The two cosines.
 * @param width The GGX alpha.
 */
double smithVisibility(const Cosines& cosines, double width);

/**
 * Return the Smith masking of one direction.
 *
 * @param cosine The cosine at the surface.
 * @param width The GGX alpha.
 */
double smithMasking(double cosine, double width);

/**
 * Return the half vector of two unit directions.
 *
 * Where they cancel, the normal comes back.
 *
 * @param outgoing The first direction.
 * @param incoming The second direction.
 * @param normal The surface normal.
 */
math::Vector halfwayOf(const math::Vector& outgoing, const math::Vector& incoming, const math::Vector& normal);

/**
 * Draw a facet normal from the facets one direction sees.
 *
 * This is Heitz's visible normal sampling, so every facet drawn faces
 * the direction. The first number sets the distance from the middle of
 * the projected disc, the second the angle around it.
 *
 * @param draw The normal, the direction, the width and the numbers.
 */
math::Vector visibleFacet(const FacetDraw& draw);

/**
 * Return the density, over reflected directions, that visibleFacet()
 * draws a direction with.
 *
 * @param query The pair of directions and the width.
 */
double visibleFacetDensity(const FacetQuery& query);

/**
 * Return the half angle a lobe of this width is known to, in radians,
 * at most one.
 *
 * @param width The GGX alpha.
 */
double lobeSharpness(double width);

/**
 * Return the unpolarised Fresnel reflectance of a dielectric boundary.
 *
 * It is one past the critical angle.
 *
 * @param cosine The cosine between the normal and the ray, both facing.
 * @param ratio The index in front of the boundary over the index behind
 *        it.
 */
double dielectricReflectance(double cosine, double ratio);

/**
 * Bend a direction through a boundary.
 *
 * @param normal The unit normal, facing the direction.
 * @param outgoing The unit direction back along the ray.
 * @param ratio The index in front over the index behind.
 */
Refraction refractedThrough(const math::Vector& normal, const math::Vector& outgoing, double ratio);

/**
 * Return Schlick's reflectance of a metal, channel by channel.
 *
 * @param faceOn The reflectance at normal incidence.
 * @param cosine The cosine between the facet and the ray.
 */
Colour schlickOf(const Colour& faceOn, double cosine);

/**
 * Return the hemispherical average of Schlick's reflectance, channel by
 * channel.
 *
 * @param faceOn The reflectance at normal incidence.
 */
Colour averagedFresnel(const Colour& faceOn);

}
