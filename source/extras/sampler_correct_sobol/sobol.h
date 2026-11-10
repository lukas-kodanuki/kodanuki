#pragma once

#include "math/random.h"

#include <cstdint>

namespace kodanuki {

/**
 * How many leading coordinates of an eye path come off the sequence:
 * the pixel, the lens and three events.
 */
constexpr std::uint64_t sobolDimensions = 28;

/**
 * How many leading coordinates the samples of a group share: the
 * pixel, the lens and the first vertex's light pair. They come off a
 * sequence of their own, placed by the group; the coordinates after
 * them come off a second one, placed by the sample.
 */
constexpr std::uint64_t sharedCoordinates = 6;

/**
 * Return a number of an Owen-scrambled Sobol sequence.
 *
 * Every coordinate is a dimension of its own of the Sobol sequence
 * (Joe and Kuo's direction numbers), so any power-of-two prefix of a
 * pixel's samples covers the coordinates jointly, not only pair by
 * pair. The shared coordinates take the sequence's first dimensions,
 * and the coordinates after them take the first dimensions again on a
 * second shuffle, so the direction off the first vertex has the
 * sequence's best pair. The sample's place is shuffled onto the
 * sequence and each coordinate scrambled by hashes over sixteen bits,
 * all seeded by the upper half of the key's stream. Pixels draw
 * unrelated points, and no two samples of a pixel land on one point.
 *
 * @param key The stream, the sample index and a coordinate below sobolDimensions.
 */
double sobolNumber(const math::Key& key);

/**
 * Return the two numbers of one pair of coordinates at once: the
 * key's even coordinate and the one after it, as two sobolNumber()
 * calls answer them. The pair shares its shuffled place.
 *
 * @param key The stream, the sample index and an even coordinate below sobolDimensions - 1.
 */
math::UnitPair sobolPair(const math::Key& key);

}
