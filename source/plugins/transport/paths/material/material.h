#pragma once

#include "core/geometry.h"
#include "math/colour.h"
#include "math/random.h"
#include "math/spectrum.h"
#include "math/vector.h"
#include "plugins/transport/paths/material/energy.h"
#include "plugins/transport/paths/material/microfacet.h"
#include "sdf/scene.h"

#include <array>
#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * Which end of a path asks the material.
 */
enum class Travel : std::uint8_t {
	/**
	 * A walk from the eye: the material draws where the light arrives
	 * from.
	 */
	FromEye,
	/**
	 * A walk from a light: the material draws where the light goes on
	 * to. Light bent through glass is carried by the squared index ratio.
	 */
	FromLight,
};

/**
 * How a glass of roughness 0 answers.
 */
enum class SmoothGlass : std::uint8_t {
	/**
	 * A true mirror and an exact passage: one direction each, drawn by
	 * the Fresnel term, that no other direction and no other strategy
	 * can find.
	 */
	Exact,
	/**
	 * A GGX lobe of the least facet width, whose faint tails catch lamps
	 * a mirror would miss.
	 */
	Lobe,
};

/**
 * The constants of a plastic's clear coat, for its Fresnel at face-on.
 */
struct Coating {
	/**
	 * The coat's average reflectance seen from inside, which bounces the
	 * body's light back.
	 */
	double front;
	/**
	 * The light that bounces twice inside the coat and is given back.
	 */
	double twice;
	/**
	 * One minus front times the escape average, the room left for the
	 * body.
	 */
	double room;
};

/**
 * A layer thin enough that light off its two faces interferes.
 */
struct Film {
	/**
	 * How deep it is, in nanometres; zero for a bare surface.
	 */
	double thickness;
	/**
	 * Its refractive index.
	 */
	double index;
};

/**
 * The light a film is met with, and the two media around it.
 */
struct FilmSides {
	/**
	 * The cosine of the angle between the ray and the normal, on the
	 * side it arrives from.
	 */
	double facing;
	/**
	 * The wavelength, in nanometres.
	 */
	double nanometres;
	/**
	 * The index the light arrives through.
	 */
	double outside;
	/**
	 * The index beneath the film.
	 */
	double beneath;
};

/**
 * What a material decides once, at load.
 *
 * Every number that depends only on the material and not on the point
 * is here: the facet width, the lobe shares and the energy tables.
 */
struct Finish {
	/**
	 * Which of the four materials it is.
	 */
	MaterialKind kind;
	/**
	 * The GGX alpha of its sharpest lobe.
	 */
	double width;
	/**
	 * The half angle its sharpest lobe is known to.
	 */
	double sharpness;
	/**
	 * Whether light focused through it is carried from the lights rather
	 * than found from the eye: glass always, metal under roughness 0.25.
	 */
	bool sharp;
	/**
	 * How often a metal draws its facet lobe rather than the cosine,
	 * the escape average held to [0.25, 1].
	 */
	double facetShare;
	/**
	 * A plastic's body luminance, of the flat colour or the mean of its
	 * ramp. Only the lobe pick reads it.
	 */
	double body;
	/**
	 * A plastic's coat constants.
	 */
	Coating coating;
	/**
	 * The refractive index of a glass's body, 1 for every other kind.
	 */
	double index;
	/**
	 * What one bounce off its facets returns.
	 */
	Escape escape;
	/**
	 * What one bounce at a glass boundary returns. It holds nothing for
	 * other kinds.
	 */
	Boundary boundary;
	/**
	 * Whether a glass loses enough at its boundary to draw broad lobes.
	 */
	bool losesMuch;
	/**
	 * Whether it is a glass of roughness 0 that answers exactly
	 * (SmoothGlass::Exact): a true mirror and an exact passage, of value
	 * and density 0 for every pair of directions asked.
	 */
	bool exact;
	/**
	 * The film the surface wears, of thickness zero for none: over a
	 * metal's facets, as a plastic's coat, or on a glass's boundary.
	 */
	Film film;
	/**
	 * How forward a scattering body's phase function leans,
	 * Henyey-Greenstein's g in (-1, 1); read only for MaterialKind::Medium.
	 */
	double lean;
};

/**
 * Every material's finish, one per Looks::materials entry.
 */
struct Materials {
	/**
	 * The finishes.
	 */
	std::vector<Finish> finishes;
	/**
	 * Whether some material is passable(), so that a line to a light
	 * may have to cross a body. Worked out once beside the finishes.
	 */
	bool passable;
};

/**
 * The refractive indices on the two sides of a surface a ray meets.
 *
 * Each lane holds its own index. They differ only where a dispersion
 * feature fills them, and then the sides are parted.
 */
struct Sides {
	/**
	 * The index where the ray travels, per lane.
	 */
	FloatSpectrum travelling;
	/**
	 * The index across the surface, per lane.
	 */
	FloatSpectrum beyond;
	/**
	 * Whether the lanes' indices differ, so that a bend keeps the hero
	 * alone.
	 */
	bool parted;
	/**
	 * Whether the ray travels inside a glass.
	 */
	bool inside;
};

/**
 * How a surface answers light at one point: a tagged struct whose
 * material's kind picks the branch of every switch.
 */
struct Bsdf {
	/**
	 * The material, an index into Materials::finishes.
	 */
	std::uint32_t material;
	/**
	 * The albedo at the point, mixed across a smooth seam.
	 */
	FloatColour albedo;
	/**
	 * The unit shading normal, facing the ray that met the point.
	 */
	math::FloatVector normal;
	/**
	 * The indices on the two sides.
	 */
	Sides sides;
};

/**
 * A surface met, and its look there.
 */
struct Shaded {
	/**
	 * How the point answers light.
	 */
	Bsdf bsdf;
	/**
	 * What the point gives off toward the ray, zero from inside.
	 */
	Colour glow;
};

/**
 * What shading reads: the scene's looks and the finishes.
 */
struct Shading {
	/**
	 * The scene: its patterns and leaves.
	 */
	const Scene& scene;
	/**
	 * The scene's looks: its materials and coats.
	 */
	const Looks& looks;
	/**
	 * The finishes worked out at load.
	 */
	const Materials& materials;
};

/**
 * A hit to shade.
 */
struct ShadingQuery {
	/**
	 * Where the ray met the surface.
	 */
	const Hit& hit;
	/**
	 * The unit direction the ray travelled.
	 */
	math::Vector heading;
	/**
	 * The material whose glass the ray travels inside, or noMaterial.
	 */
	std::uint32_t medium;
	/**
	 * The registers the albedo patterns run in.
	 */
	std::vector<double>& registers;
};

/**
 * A point of a leaf's surface to read a pattern at.
 */
struct PatternSpot {
	/**
	 * The scene: its leaves, programs and arguments.
	 */
	const Scene& scene;
	/**
	 * The leaf whose own space a pattern is read in, or noLeaf for the
	 * world's.
	 */
	std::uint32_t leaf;
	/**
	 * The point in world space.
	 */
	math::Vector point;
	/**
	 * The unit normal in world space, facing the ray.
	 */
	math::Vector normal;
};

/**
 * A pair of directions at a surface.
 */
struct BsdfPair {
	/**
	 * The unit direction the light leaves to.
	 */
	math::Vector outgoing;
	/**
	 * The unit direction the light arrives from.
	 */
	math::Vector incoming;
	/**
	 * The wavelengths asked at.
	 */
	const Wavelengths& wavelengths;
};

/**
 * The numbers a material draws one direction with.
 */
struct LobeNumbers {
	/**
	 * The pair a facet or a cosine direction is drawn from.
	 */
	math::UnitPair pair;
	/**
	 * The number that picks a lobe.
	 */
	double lobe;
	/**
	 * The number glass picks reflection or passage with.
	 */
	double side;
};

/**
 * A direction to draw.
 */
struct BsdfDraw {
	/**
	 * The unit direction the walk arrived along, turned round.
	 */
	math::Vector outgoing;
	/**
	 * Which end asks.
	 */
	Travel travel;
	/**
	 * The numbers.
	 */
	LobeNumbers numbers;
	/**
	 * The wavelengths.
	 */
	const Wavelengths& wavelengths;
};

/**
 * What a surface answers about one pair of directions.
 */
struct Response {
	/**
	 * The BSDF value per lane, without the cosine.
	 */
	Spectrum4 value;
	/**
	 * The solid-angle density sampleBsdf() draws the incoming direction
	 * with, at the hero.
	 */
	double density;
};

/**
 * The density of a pair and of the pair turned round, as
 * densitiesOf() answers them.
 */
struct BsdfDensities {
	/**
	 * bsdfDensity() of the pair.
	 */
	double density;
	/**
	 * bsdfDensity() of the pair turned round on the same surface.
	 */
	double reverse;
};

/**
 * What a surface answers a pair with, and the density of the pair turned
 * round.
 */
struct BsdfAnswer {
	/**
	 * The value, as Response::value.
	 */
	Spectrum4 value;
	/**
	 * The density of drawing the incoming direction, as
	 * Response::density.
	 */
	double density;
	/**
	 * The density of drawing the outgoing direction from the incoming
	 * one on the same surface.
	 */
	double reverse;
};

/**
 * One outgoing direction at a surface and what it fixes for every
 * incoming one: the surface seen from a vertex that offers several
 * lights.
 */
struct BsdfView {
	/**
	 * The unit direction toward the viewer.
	 */
	math::Vector outgoing;
	/**
	 * The wavelengths the answers are read at.
	 */
	const Wavelengths& wavelengths;
};

/**
 * What a rough finish works out once for one outgoing direction, read
 * by answerViewed() for any incoming one.
 */
struct ViewedBsdf {
	/**
	 * The finish.
	 */
	const Finish& finish;
	/**
	 * The surface.
	 */
	const Bsdf& bsdf;
	/**
	 * The outgoing direction and the wavelengths.
	 */
	BsdfView view;
	/**
	 * Whether the finish is rough (plastic or metal), so that the rest
	 * is filled; answerViewed() asks the plain answers otherwise.
	 */
	bool rough;
	/**
	 * The cosine of the outgoing direction to the normal.
	 */
	double leaving;
	/**
	 * The escape table read at leaving.
	 */
	double left;
	/**
	 * The facet lobe's share of the draws at leaving.
	 */
	double share;
	/**
	 * The Smith masking at leaving.
	 */
	double masking;
	/**
	 * A plastic's diffuse body per lane: its albedo over pi.
	 */
	Spectrum4 body;
	/**
	 * A metal's light returned after a second bounce, per lane.
	 */
	Spectrum4 twice;
	/**
	 * A filmed plastic's coat per lane.
	 */
	std::array<Coating, spectrumLanes> coats;
};

/**
 * One incoming direction asked of a ViewedBsdf.
 */
struct ViewedAsk {
	/**
	 * The unit incoming direction.
	 */
	math::Vector incoming;
	/**
	 * Whether the turned density is wanted; it is 0 otherwise.
	 */
	bool reverse;
};

/**
 * A direction a material drew.
 */
struct Scatter {
	/**
	 * Whether a direction was drawn. Nothing else is meaningful where it
	 * was not.
	 */
	bool found;
	/**
	 * The unit direction drawn.
	 */
	math::Vector incoming;
	/**
	 * The value times the cosine over the density, per lane.
	 */
	Spectrum4 weight;
	/**
	 * The density the direction was drawn with.
	 */
	double density;
	/**
	 * The half angle the direction is known to.
	 */
	double sharpness;
	/**
	 * Whether the direction passes through the surface.
	 */
	bool through;
};

/**
 * Return how much of the light a film between two media sends back at
 * one wavelength: the mean of the two polarisations' Airy sums over the
 * film's two faces, one where either face reflects totally.
 *
 * @param film The film.
 * @param sides The cosine met at, the wavelength and the indices on the
 *        two sides.
 */
double filmReflectance(const Film& film, const FilmSides& sides);

/**
 * Return the finish a collision inside a scattering body stands on:
 * Henyey-Greenstein's phase function of a lean, drawn exactly, with no
 * cosine, never sharp and never exact.
 *
 * @param lean Henyey-Greenstein's g.
 */
Finish phaseFinish(double lean);

/**
 * Whether a line may pass straight through a body of the material: a
 * glass of index 1 without dispersion or a film, which bends nothing.
 *
 * @param material The material.
 */
bool passable(const Material& material);

/**
 * Work out one material's finish.
 *
 * @param material The material.
 * @param smooth How a glass of roughness 0 answers.
 */
Finish finishOf(const Material& material, SmoothGlass smooth);

/**
 * Work out one material's finish beside finishes already made.
 *
 * The energy tables depend on the facet width alone, so a finish of
 * the same width in made lends its tables rather than having them
 * worked out again. The finish is the one finishOf() answers.
 *
 * @param made The finishes made so far.
 * @param material The material.
 * @param smooth How a glass of roughness 0 answers.
 */
Finish finishAmong(const Materials& made, const Material& material, SmoothGlass smooth);

/**
 * Work out every material's finish.
 *
 * The energy tables are integrated here, once a material.
 *
 * @param looks The scene's looks: its materials and coats.
 * @param smooth How a glass of roughness 0 answers.
 */
Materials materialsOf(const Looks& looks, SmoothGlass smooth);

/**
 * Return a pattern's value at a spot: its program run at the point and
 * normal, in the leaf's own space unless the pattern reads the world's.
 *
 * @param spot The point, its normal and its leaf.
 * @param pattern The program and arguments.
 * @param registers The registers the program runs in.
 */
double patternAt(const PatternSpot& spot, const Albedo& pattern, std::vector<double>& registers);

/**
 * Return an albedo at a spot: its flat colour, or its ramp at the
 * pattern's value there.
 *
 * @param spot The point, its normal and its leaf.
 * @param albedo The albedo.
 * @param registers The registers the pattern runs in.
 */
Colour albedoAt(const PatternSpot& spot, const Albedo& albedo, std::vector<double>& registers);

/**
 * Return the light a material gives off: its glow times its strength.
 *
 * @param material The material.
 */
Colour glowOf(const Material& material);

/**
 * Shade a hit: its albedo, normal, sides and glow.
 *
 * The albedo runs the material's pattern at the point, in the leaf's own
 * coordinates unless the pattern reads the world's, and across a smooth
 * seam it mixes in the other leaf's albedo by the hit's share. The glow
 * mixes the same way. The ray travels in its medium's index, and across
 * the surface lies the open air from inside a glass, or the material's
 * body from outside.
 *
 * @param shading The scene and the finishes.
 * @param query The hit, the heading, the medium and the registers.
 */
Shaded bsdfAt(const Shading& shading, const ShadingQuery& query);

/**
 * Return a surface's value and density for one pair of directions.
 *
 * Matte is Lambert. Metal is GGX with Schlick plus its compensation
 * lobe. Plastic is a diffuse body under a clear GGX coat of index 1.5,
 * compensated. Glass is a rough dielectric whose reflection and passage
 * follow the exact Fresnel term, plus the two broad lobes its boundary
 * table gives back; an exact glass answers 0 with density 0, as no
 * pair of directions asked is its mirror or passage. A value that
 * differs by lane follows each lane's index. The density follows the
 * hero.
 *
 * @param materials The finishes.
 * @param bsdf The surface.
 * @param pair The two directions.
 */
Response respondBsdf(const Materials& materials, const Bsdf& bsdf, const BsdfPair& pair);

/**
 * Work out what a surface fixes for one outgoing direction.
 *
 * @param materials The finishes.
 * @param bsdf The surface.
 * @param view The outgoing direction and the wavelengths.
 */
ViewedBsdf viewedBsdf(const Materials& materials, const Bsdf& bsdf, const BsdfView& view);

/**
 * Return respondBsdf() of the viewed outgoing direction and one incoming
 * direction, with bsdfDensity() of the pair turned round where asked,
 * reading what the view fixed.
 *
 * @param materials The finishes.
 * @param viewed The surface seen from its outgoing direction.
 * @param ask The incoming direction and whether the turned density is wanted.
 */
BsdfAnswer answerViewed(const Materials& materials, const ViewedBsdf& viewed, const ViewedAsk& ask);

/**
 * Return a pair's density and the density of the pair turned round on
 * the same surface, as answerViewed() answers them without the value.
 *
 * @param materials The finishes.
 * @param bsdf The surface.
 * @param pair The two directions.
 */
BsdfDensities densitiesOf(const Materials& materials, const Bsdf& bsdf, const BsdfPair& pair);

/**
 * Return only the value respondBsdf() answers, without its density.
 *
 * Callers that weigh the pair by another density ask this.
 *
 * @param materials The finishes.
 * @param bsdf The surface.
 * @param pair The two directions.
 */
Spectrum4 bsdfValue(const Materials& materials, const Bsdf& bsdf, const BsdfPair& pair);

/**
 * Return only the density sampleBsdf() draws a pair with.
 *
 * The MIS engine asks it for reverse densities.
 *
 * @param materials The finishes.
 * @param bsdf The surface.
 * @param pair The two directions.
 */
double bsdfDensity(const Materials& materials, const Bsdf& bsdf, const BsdfPair& pair);

/**
 * Draw a direction off a surface.
 *
 * The lobe number picks the facet lobe or the cosine (metal, plastic),
 * or the broad lobe (glass). The side number picks glass's reflection
 * or passage by the hero's Fresnel term. The facet is drawn from the
 * visible normals. The weight is the value times the cosine over the
 * density, and a walk from a light carries a bend by the squared index
 * ratio. An exact glass mirrors or bends about the normal itself, with
 * density 0 and the weight its Fresnel term over the chance of the side
 * drawn.
 *
 * @param materials The finishes.
 * @param bsdf The surface.
 * @param draw The direction arrived along, the end and the numbers.
 */
Scatter sampleBsdf(const Materials& materials, const Bsdf& bsdf, const BsdfDraw& draw);

/**
 * Return the surface as seen from its other side: the normal turned
 * round and the two sides swapped.
 *
 * A density of light that passes through is asked of it the other way.
 *
 * @param bsdf The surface.
 */
Bsdf turnedBsdf(const Bsdf& bsdf);

/**
 * Return whether a material is sharp.
 *
 * Paths from the lights pass on through sharp surfaces, and they are
 * joined at surfaces that are not.
 *
 * @param materials The finishes.
 * @param material The material.
 */
bool isSharp(const Materials& materials, std::uint32_t material);

/**
 * Return whether a ray may go into a material's body: a glass.
 *
 * @param materials The finishes.
 * @param material The material.
 */
bool isPassable(const Materials& materials, std::uint32_t material);

/**
 * Return whether a material answers exactly, so that no light can be
 * aimed at through it.
 *
 * @param materials The finishes.
 * @param material The material.
 */
bool isExact(const Materials& materials, std::uint32_t material);

}
