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
 * Return a number of the Owen-scrambled Sobol sequence.
 *
 * Coordinates come in pairs, each pair the first two Sobol dimensions,
 * so any power-of-two prefix of a pixel's samples covers the unit square
 * evenly. The pair's place is shuffled onto the sequence and the
 * coordinate scrambled by hashes over sixteen bits, both seeded by the
 * upper half of the key's stream. Pixels draw unrelated points, and no
 * two samples of a pixel land on one point.
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
