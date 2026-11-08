#pragma once

#include "spectral/spectrum.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * A colour in linear sRGB, in double precision.
 *
 * The scene's albedos, glows and powers are colours. A pixel's mean
 * is one too.
 */
struct Colour {
	/**
	 * The red channel.
	 */
	double red;
	/**
	 * The green channel.
	 */
	double green;
	/**
	 * The blue channel.
	 */
	double blue;
};

/**
 * Add two colours channel by channel.
 *
 * @param a The first colour.
 * @param b The second colour.
 */
Colour operator+(const Colour& a, const Colour& b);

/**
 * Multiply two colours channel by channel.
 *
 * @param a The first colour.
 * @param b The second colour.
 */
Colour operator*(const Colour& a, const Colour& b);

/**
 * Scale a colour by a number.
 *
 * @param a The colour.
 * @param s The number.
 */
Colour operator*(const Colour& a, double s);

/**
 * Return the colour three floats of the scene hold.
 *
 * @param channels The red, green and blue channels.
 */
Colour colourFrom(const float* channels);

/**
 * Return the colour the first three floats of a list hold, as the scene
 * language gives colours.
 *
 * @param from The list, at least three long.
 */
Colour colourFromFloats(const std::vector<float>& from);

/**
 * Return the Rec. 709 luminance of a colour.
 *
 * @param colour The colour.
 */
double luminanceOf(const Colour& colour);

/**
 * Return the mean of a colour's three channels.
 *
 * @param colour The colour.
 */
double channelMean(const Colour& colour);

/**
 * Return what a colour is worth at the four lanes a path carries.
 *
 * The colour is split into its smallest channel, flat; the step to the
 * middle channel, over everything but the smallest channel's basis; and
 * the step to the largest, over the largest channel's basis. A grey comes
 * out flat, so a neutral emitter keeps its exact brightness at any
 * wavelength. Reflectances and emissions share this one map.
 *
 * @param colour The colour, not negative.
 * @param wavelengths The four wavelengths.
 */
Spectrum4 spectrumOf(const Colour& colour, const Wavelengths& wavelengths);

/**
 * Return the colour of a spectral estimate.
 *
 * Each lane with a density above zero adds its value over its density,
 * weighted by the CIE 1931 matching functions (a fit of skewed Gaussian
 * lobes tabled every 5 nm). The lanes are averaged and the result is
 * scaled so that a flat unit spectrum comes out as (1, 1, 1).
 *
 * @param wavelengths The four wavelengths the estimate was carried at.
 * @param spectrum The estimate.
 */
Colour colourOf(const Wavelengths& wavelengths, const Spectrum4& spectrum);

/**
 * Return the density a path's wavelengths are drawn with, per
 * nanometre.
 *
 * Seven tenths of it follow how much colourOf() makes of a unit spectrum
 * at a wavelength, summed over the channels without sign, a 5 nm slice
 * at a time. Three tenths are even. It is held as 256 intervals with an
 * equal share of the draws each, so a draw is a look-up.
 *
 * @param nanometres The wavelength, inside the visible range.
 */
double wavelengthDensity(double nanometres);

/**
 * Draw the four wavelengths of a path from one number.
 *
 * The number and its three turns by a quarter, wrapped into [0, 1), are
 * each carried through the inverse of wavelengthDensity(). Lane 0 is the
 * hero.
 *
 * @param u A number in [0, 1).
 */
Wavelengths sampledWavelengths(double u);

/**
 * Return the number a sample's wavelengths are drawn from.
 *
 * It steps along the sample index by the golden ratio from a start the
 * pixel draws, so any run of a pixel's samples spreads its hero
 * wavelengths evenly.
 *
 * @param start The pixel's start, in [0, 1).
 * @param index The sample's place in the pixel's whole run.
 */
double wavelengthShare(double start, std::uint32_t index);

}
