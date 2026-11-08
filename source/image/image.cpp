#include "image/image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace kodanuki {

constexpr double filmicCeiling   = 2.51 / 2.43;
constexpr double filmicSaturated = 16.0;
constexpr double byteLevels      = 255.0;

Image blankImage(int width, int height) {
	return Image {
		.width  = width,
		.height = height,
		.rgb    = std::vector<float>(std::size_t(width) * std::size_t(height) * 3, 0.0f),
	};
}

double filmicCurve(double value) {
	if (value >= filmicSaturated) {
		return filmicCeiling;
	}
	return value * (2.51 * value + 0.03) / (value * (2.43 * value + 0.59) + 0.14);
}

double boundedShare(double value) {
	if (!(value > 0)) {
		return 0;
	}
	return value < 1 ? value : 1;
}

double displayed(double value, const Grade& grade) {
	if (!(value > 0)) {
		return 0;
	}
	const double exposed = value * double(grade.exposure);
	const double mapped  = grade.curve == Curve::Filmic ? filmicCurve(exposed) : exposed;
	return std::pow(boundedShare(mapped), 1 / 2.2);
}

double stepsBetween(double low, double high, const Grade& grade) {
	return std::abs(displayed(high, grade) - displayed(low, grade)) * byteLevels;
}

unsigned char encodedByte(double value, const Grade& grade) {
	return static_cast<unsigned char>(std::lround(displayed(value, grade) * byteLevels));
}

std::ofstream openedFile(const std::string& path) {
	std::ofstream out(path, std::ios::binary);
	if (!out) {
		std::fprintf(stderr, "%s:1:1: cannot write\n", path.c_str());
		std::exit(1);
	}
	return out;
}

void writeBytes(const Image& image, const std::vector<unsigned char>& bytes, const std::string& path) {
	std::ofstream out = openedFile(path);
	out << "P6\n"
		<< image.width << " " << image.height << "\n255\n";
	out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

void writePpm(const Image& image, const Grade& grade, const std::string& path) {
	std::vector<unsigned char> bytes(image.rgb.size());
	for (std::size_t i = 0; i < bytes.size(); ++i) {
		bytes[i] = encodedByte(double(image.rgb[i]), grade);
	}
	writeBytes(image, bytes, path);
}

void writeUngradedPpm(const Image& image, const std::string& path) {
	std::vector<unsigned char> bytes(image.rgb.size());
	for (std::size_t i = 0; i < bytes.size(); ++i) {
		bytes[i] = static_cast<unsigned char>(std::lround(boundedShare(double(image.rgb[i])) * byteLevels));
	}
	writeBytes(image, bytes, path);
}

void writePfm(const Image& image, const std::string& path) {
	std::ofstream out = openedFile(path);
	out << "PF\n"
		<< image.width << " " << image.height << "\n-1\n";
	const std::size_t row = std::size_t(image.width) * 3;
	for (int y = image.height - 1; y >= 0; --y) {
		out.write(reinterpret_cast<const char*>(image.rgb.data() + std::size_t(y) * row), std::streamsize(row * sizeof(float)));
	}
}

}
