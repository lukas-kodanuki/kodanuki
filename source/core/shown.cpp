#include "core/shown.h"

#include <array>

namespace kodanuki {

Lens shownLens(const Lens& drawn, const FilmPlan& film) {
	Lens shown   = drawn;
	shown.width  = drawn.width * film.factor;
	shown.height = drawn.height * film.factor;
	return shown;
}

std::size_t shownPixelOf(const ShownFrame& frame, std::size_t drawn) {
	const int factor  = frame.film.factor;
	const int columns = frame.record.width;
	const int x       = int(drawn % std::size_t(columns)) * factor + frame.film.phase % factor;
	const int y       = int(drawn / std::size_t(columns)) * factor + frame.film.phase / factor;
	return std::size_t(y) * std::size_t(frame.width) + std::size_t(x);
}

int bayerPhase(int factor, std::uint32_t place) {
	constexpr std::array<int, 4> pairs     = { 0, 3, 1, 2 };
	constexpr std::array<int, 9> thirds    = { 0, 7, 5, 2, 6, 4, 3, 1, 8 };
	constexpr std::array<int, 16> quarters = { 0, 10, 2, 8, 5, 15, 7, 13, 1, 11, 3, 9, 4, 14, 6, 12 };
	switch (factor) {
		case 2: return pairs[place];
		case 3: return thirds[place];
		case 4: return quarters[place];
	}

	return 0;
}

FilmPlan bayerFilm(int factor, std::uint32_t index) {
	const std::uint32_t place = index % std::uint32_t(factor * factor);
	return FilmPlan {
		.factor = factor,
		.phase  = bayerPhase(factor, place),
	};
}

const std::vector<Surface>& noSurfaces() {
	static const std::vector<Surface> none = {};
	return none;
}

FrameRecord emptyFrameRecord() {
	return FrameRecord {
		.width    = 0,
		.height   = 0,
		.parts    = {},
		.surfaces = {},
	};
}

}
