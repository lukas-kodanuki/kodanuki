#pragma once

namespace kodanuki {

/**
 * How many wavelengths one ray carries.
 */
constexpr int spectrumLanes = 4;

/**
 * The shortest wavelength a path carries, in nanometres.
 */
constexpr double shortestWavelength = 380.0;

/**
 * The longest wavelength a path carries, in nanometres.
 */
constexpr double longestWavelength = 700.0;

/**
 * The number of places a spectrum table holds.
 */
constexpr int tablePlaces = 33;

/**
 * A radiometric quantity at the four wavelengths a path carries.
 *
 * Lane 0 is the hero. A lane holds what the quantity is at that lane's
 * wavelength, not divided by the density it was drawn with.
 */
struct Spectrum4 {
	/**
	 * The value at each lane.
	 */
	double values[4];
};

/**
 * The four wavelengths a path carries, and how each was drawn.
 *
 * The hero is drawn and the three companions sit a quarter of the unit
 * interval on from it, each carried through the same inverse
 * distribution. Each lane keeps its own density, so each lane is a fair
 * estimate alone. The responses of the three basis curves are worked out
 * once here, for upsampling.
 */
struct Wavelengths {
	/**
	 * The wavelength of each lane, in nanometres.
	 */
	double nanometres[4];
	/**
	 * The density each lane was drawn with, per nanometre. A lane at zero
	 * carries nothing.
	 */
	double density[4];
	/**
	 * What the red basis curve answers at each lane.
	 */
	Spectrum4 red;
	/**
	 * What the green basis curve answers at each lane.
	 */
	Spectrum4 green;
	/**
	 * What the blue basis curve answers at each lane.
	 */
	Spectrum4 blue;
};

/**
 * A quantity known at evenly spaced places over the visible range, read
 * between them by straight lines.
 */
struct SpectrumTable {
	/**
	 * The values, the first at the shortest wavelength and the last at
	 * the longest.
	 */
	double values[tablePlaces];
};

/**
 * Add two spectra lane by lane.
 *
 * @param a The first spectrum.
 * @param b The second spectrum.
 */
Spectrum4 operator+(const Spectrum4& a, const Spectrum4& b);

/**
 * Subtract one spectrum from another lane by lane.
 *
 * @param a The spectrum subtracted from.
 * @param b The spectrum subtracted.
 */
Spectrum4 operator-(const Spectrum4& a, const Spectrum4& b);

/**
 * Multiply two spectra lane by lane.
 *
 * @param a The first spectrum.
 * @param b The second spectrum.
 */
Spectrum4 operator*(const Spectrum4& a, const Spectrum4& b);

/**
 * Scale every lane by one number.
 *
 * @param a The spectrum.
 * @param s The number.
 */
Spectrum4 operator*(const Spectrum4& a, double s);

/**
 * Divide every lane by one number that is not zero.
 *
 * @param a The spectrum.
 * @param s The number.
 */
Spectrum4 operator/(const Spectrum4& a, double s);

/**
 * Return a spectrum with every lane at one value.
 *
 * @param value The value.
 */
Spectrum4 flatSpectrum(double value);

/**
 * Return the mean over the four lanes.
 *
 * Light selection weighs its offers by it.
 *
 * @param spectrum The spectrum.
 */
double laneMean(const Spectrum4& spectrum);

/**
 * Return the largest lane.
 *
 * Roulette judges a throughput by it.
 *
 * @param spectrum The spectrum.
 */
double laneMaximum(const Spectrum4& spectrum);

/**
 * Return a throughput that only the hero carries on.
 *
 * The companions are zeroed and the hero is scaled by four, so that
 * once the lanes are averaged into a colour the hero stands for all
 * four. What the lanes gathered before is untouched.
 *
 * @param throughput The throughput.
 */
Spectrum4 collapsedToHero(const Spectrum4& throughput);

/**
 * Return where a wavelength falls among a table's places, from 0 at
 * the shortest wavelength to tablePlaces - 1 at the longest, clamped.
 *
 * @param nanometres The wavelength.
 */
double tablePlace(double nanometres);

/**
 * Return a table's value at each lane's wavelength, read between its
 * places by straight lines.
 *
 * @param table The table.
 * @param wavelengths The wavelengths.
 */
Spectrum4 spectrumTableAt(const SpectrumTable& table, const Wavelengths& wavelengths);

/**
 * Return four wavelengths spread evenly over the visible range.
 *
 * The first sits at u along the range and the others a quarter of the
 * range apart, wrapped, each with the uniform density. This is for what
 * is integrated rather than sampled, such as a tint.
 *
 * @param u A number in [0, 1).
 */
Wavelengths spreadWavelengths(double u);

/**
 * Return four wavelengths with the basis responses filled in.
 *
 * @param nanometres The wavelength of each lane.
 * @param density The density each lane was drawn with.
 */
Wavelengths wavelengthsAt(const double* nanometres, const double* density);

/**
 * Return what the red basis curve answers at a wavelength.
 *
 * It is zero below 575 nm, one above 605 nm, and a smooth step between.
 *
 * @param nanometres The wavelength.
 */
double basisRed(double nanometres);

/**
 * Return what the green basis curve answers at a wavelength.
 *
 * The three curves sum to one at every wavelength.
 *
 * @param nanometres The wavelength.
 */
double basisGreen(double nanometres);

/**
 * Return what the blue basis curve answers at a wavelength.
 *
 * It is one below 445 nm, zero above 530 nm, and a smooth step between.
 *
 * @param nanometres The wavelength.
 */
double basisBlue(double nanometres);

}
