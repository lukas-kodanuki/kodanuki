#include "extras/sampler_correct_sobol/sobol.h"

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

std::uint32_t secondDimension(std::uint32_t index) {
	index ^= (index >> 1) & 0x55555555U;
	index ^= (index >> 2) & 0x33333333U;
	index ^= (index >> 4) & 0x0F0F0F0FU;
	index ^= (index >> 8) & 0x00FF00FFU;
	return index ^ ((index >> 16) & 0x0000FFFFU);
}

std::uint32_t pairPlace(const math::Key& key) {
	const std::uint32_t scramble  = std::uint32_t(key.stream >> 32);
	const std::uint32_t dimension = std::uint32_t(key.coordinate);
	return shuffledIndex(std::uint32_t(key.index), stirred(scramble + (dimension >> 1) * 0x9E3779B9U));
}

double placedNumber(const math::Key& key, std::uint32_t place) {
	const std::uint32_t scramble  = std::uint32_t(key.stream >> 32);
	const std::uint32_t dimension = std::uint32_t(key.coordinate);
	const std::uint32_t seed      = stirred(scramble + dimension * 0xA136AAADU + 0x2545F491U);
	const std::uint32_t value     = (dimension & 1U) != 0 ? secondDimension(place) : place;
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
