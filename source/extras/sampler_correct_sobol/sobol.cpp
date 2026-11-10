#include "extras/sampler_correct_sobol/sobol.h"

#include <array>
#include <cstddef>

namespace kodanuki {

std::uint32_t stirred(std::uint32_t value) {
	value ^= value >> 16;
	value *= 0x7FEB352DU;
	value ^= value >> 15;
	value *= 0x846CA68BU;
	return value ^ (value >> 16);
}

std::uint32_t reversedBits(std::uint32_t value) {
	value = (value << 16) | (value >> 16);
	value = ((value & 0x00FF00FFU) << 8) | ((value & 0xFF00FF00U) >> 8);
	value = ((value & 0x0F0F0F0FU) << 4) | ((value & 0xF0F0F0F0U) >> 4);
	value = ((value & 0x33333333U) << 2) | ((value & 0xCCCCCCCCU) >> 2);
	return ((value & 0x55555555U) << 1) | ((value & 0xAAAAAAAAU) >> 1);
}

std::uint32_t reversedSixteen(std::uint32_t value) {
	value = ((value & 0x00FFU) << 8) | ((value & 0xFF00U) >> 8);
	value = ((value & 0x0F0FU) << 4) | ((value & 0xF0F0U) >> 4);
	value = ((value & 0x3333U) << 2) | ((value & 0xCCCCU) >> 2);
	return ((value & 0x5555U) << 1) | ((value & 0xAAAAU) >> 1);
}

std::uint32_t laineKarras(std::uint32_t value, std::uint32_t seed) {
	value += seed;
	value ^= value * 0x6C50B47CU;
	value ^= value * 0xB82F1E52U;
	value ^= value * 0xC7AFE638U;
	return value ^ (value * 0x8D22F6E6U);
}

std::uint32_t shuffledIndex(std::uint32_t index, std::uint32_t seed) {
	return reversedSixteen(laineKarras(reversedSixteen(index & 0xFFFFU), seed) & 0xFFFFU);
}

constexpr int sobolBits = 16;

struct SobolRule {
	int degree;
	int polynomial;
	std::array<std::uint32_t, 7> first;
};

using SobolColumns = std::array<std::uint32_t, sobolBits>;
using SobolTable   = std::array<std::array<std::array<std::uint32_t, 256>, 2>, sobolDimensions>;

const std::array<SobolRule, sobolDimensions> sobolRules = { {
	{ 0, 0, { 1, 1, 1, 1, 1, 1, 1 } },
	{ 1, 0, { 1, 0, 0, 0, 0, 0, 0 } },
	{ 2, 1, { 1, 3, 0, 0, 0, 0, 0 } },
	{ 3, 1, { 1, 3, 1, 0, 0, 0, 0 } },
	{ 3, 2, { 1, 1, 1, 0, 0, 0, 0 } },
	{ 4, 1, { 1, 1, 3, 3, 0, 0, 0 } },
	{ 4, 4, { 1, 3, 5, 13, 0, 0, 0 } },
	{ 5, 2, { 1, 1, 5, 5, 17, 0, 0 } },
	{ 5, 4, { 1, 1, 5, 5, 5, 0, 0 } },
	{ 5, 7, { 1, 1, 7, 11, 19, 0, 0 } },
	{ 5, 11, { 1, 1, 5, 1, 1, 0, 0 } },
	{ 5, 13, { 1, 1, 1, 3, 11, 0, 0 } },
	{ 5, 14, { 1, 3, 5, 5, 31, 0, 0 } },
	{ 6, 1, { 1, 3, 3, 9, 7, 49, 0 } },
	{ 6, 13, { 1, 1, 1, 15, 21, 21, 0 } },
	{ 6, 16, { 1, 3, 1, 13, 27, 49, 0 } },
	{ 6, 19, { 1, 1, 1, 15, 7, 5, 0 } },
	{ 6, 22, { 1, 3, 1, 15, 13, 25, 0 } },
	{ 6, 25, { 1, 1, 5, 5, 19, 61, 0 } },
	{ 7, 1, { 1, 3, 7, 11, 23, 15, 103 } },
	{ 7, 4, { 1, 3, 7, 13, 13, 15, 69 } },
	{ 7, 7, { 1, 1, 3, 13, 7, 35, 63 } },
	{ 7, 8, { 1, 3, 5, 9, 1, 25, 53 } },
	{ 7, 14, { 1, 3, 1, 13, 9, 35, 107 } },
	{ 7, 19, { 1, 3, 1, 5, 27, 61, 31 } },
	{ 7, 21, { 1, 1, 5, 11, 19, 41, 61 } },
	{ 7, 28, { 1, 3, 5, 3, 3, 13, 69 } },
	{ 7, 31, { 1, 1, 7, 13, 1, 19, 1 } },
} };

std::uint32_t nextDirection(const SobolRule& rule, const SobolColumns& numbers, int bit) {
	const int degree     = rule.degree;
	std::uint32_t number = numbers[std::size_t(bit - degree)] ^ (numbers[std::size_t(bit - degree)] << degree);
	for (int k = 1; k < degree; ++k) {
		const std::uint32_t term = std::uint32_t((rule.polynomial >> (degree - 1 - k)) & 1);
		number ^= term * (numbers[std::size_t(bit - k)] << k);
	}
	return number;
}

SobolColumns directionNumbers(const SobolRule& rule) {
	SobolColumns numbers = {};
	for (int bit = 0; bit < sobolBits; ++bit) {
		const bool given          = rule.degree == 0 || bit < rule.degree;
		numbers[std::size_t(bit)] = given ? rule.first[std::size_t(rule.degree == 0 ? 0 : bit)] : nextDirection(rule, numbers, bit);
	}
	return numbers;
}

std::uint32_t placedBits(const SobolColumns& numbers, std::uint32_t place) {
	std::uint32_t value = 0;
	for (int bit = 0; bit < sobolBits; ++bit) {
		const std::uint32_t column = reversedBits(numbers[std::size_t(bit)] << (31 - bit));
		value ^= ((place >> bit) & 1U) != 0 ? column : 0U;
	}
	return value;
}

SobolTable builtTable() {
	SobolTable table = {};
	for (std::size_t dimension = 0; dimension < sobolDimensions; ++dimension) {
		const SobolColumns numbers = directionNumbers(sobolRules[dimension]);
		for (std::uint32_t byte = 0; byte < 256; ++byte) {
			table[dimension][0][byte] = placedBits(numbers, byte);
			table[dimension][1][byte] = placedBits(numbers, byte << 8);
		}
	}
	return table;
}

const SobolTable& sobolTable() {
	static const SobolTable table = builtTable();
	return table;
}

std::uint32_t pairPlace(const math::Key& key) {
	const std::uint32_t own = key.coordinate < sharedCoordinates ? 0U : 0x68E31DA4U;
	return shuffledIndex(std::uint32_t(key.index), stirred(std::uint32_t(key.stream >> 32) + own));
}

std::uint64_t sequenceDimension(std::uint64_t coordinate) {
	return coordinate < sharedCoordinates ? coordinate : coordinate - sharedCoordinates;
}

double placedNumber(const math::Key& key, std::uint32_t place) {
	const std::uint32_t scramble  = std::uint32_t(key.stream >> 32);
	const std::uint32_t dimension = std::uint32_t(key.coordinate);
	const std::uint32_t seed      = stirred(scramble + dimension * 0xA136AAADU + 0x2545F491U);
	const auto& columns           = sobolTable()[sequenceDimension(key.coordinate)];
	const std::uint32_t value     = columns[0][place & 0xFFU] ^ columns[1][(place >> 8) & 0xFFU];
	return reversedBits(laineKarras(value, seed)) * 0x1p-32;
}

double sobolNumber(const math::Key& key) {
	return placedNumber(key, pairPlace(key));
}

math::UnitPair sobolPair(const math::Key& key) {
	const std::uint32_t place = pairPlace(key);
	math::Key second          = key;
	second.coordinate         = key.coordinate + 1;
	return { placedNumber(key, place), placedNumber(second, place) };
}

}
