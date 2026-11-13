#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kodanuki {

/**
 * How graded colours are pressed into the displayable range.
 */
enum class Curve : std::uint8_t {
	/**
	 * A filmic shoulder that rolls highlights off.
	 */
	Filmic,
	/**
	 * Straight, clamped at one.
	 */
	Clamped,
};

/**
 * How light is turned into displayed colour.
 */
struct Grade {
	/**
	 * What the light is multiplied by first.
	 */
	float exposure;
	/**
	 * The curve.
	 */
	Curve curve;
};

/**
 * A frame of linear radiance, three floats a pixel by rows from the top
 * left.
 */
struct Image {
	/**
	 * The width in pixels.
	 */
	int width;
	/**
	 * The height in pixels.
	 */
	int height;
	/**
	 * The channels.
	 */
	std::vector<float> rgb;
};

/**
 * Return a black image.
 *
 * @param width The width.
 * @param height The height.
 */
Image blankImage(int width, int height);

/**
 * Return the share of full scale a screen shows for a linear value.
 *
 * The value is exposed, bent through the clamped or filmic curve
 * (Narkowicz's fit, saturating at 16), and gamma-corrected by 1/2.2.
 * A negative value or one that is not a number shows nothing.
 *
 * @param value The linear value.
 * @param grade The exposure and the curve.
 */
double displayed(double value, const Grade& grade);

/**
 * Return how many steps of a byte a screen shows between two linear
 * values.
 *
 * @param low The first value.
 * @param high The second value.
 * @param grade The exposure and the curve.
 */
double stepsBetween(double low, double high, const Grade& grade);

/**
 * Return a linear value as a byte, rounded.
 *
 * @param value The linear value.
 * @param grade The exposure and the curve.
 */
unsigned char encodedByte(double value, const Grade& grade);

/**
 * Write an image graded as a binary PPM.
 *
 * A file that cannot be written prints `path:1:1: cannot write` and
 * exits.
 *
 * @param image The image.
 * @param grade The grade.
 * @param path The file.
 */
void writePpm(const Image& image, const Grade& grade, const std::string& path);

/**
 * Write an image whose values are already shares of full scale as a
 * binary PPM, each rounded to a byte.
 *
 * The normals image is written so.
 *
 * @param image The image, every value in [0, 1].
 * @param path The file.
 */
void writeUngradedPpm(const Image& image, const std::string& path);

/**
 * Write an image's linear values as a little-endian PFM, bottom row
 * first.
 *
 * @param image The image.
 * @param path The file.
 */
void writePfm(const Image& image, const std::string& path);

}
