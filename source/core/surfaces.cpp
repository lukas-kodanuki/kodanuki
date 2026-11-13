#include "core/surfaces.h"

#include "core/frame.h"
#include "core/render.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace kodanuki {

struct SurfaceFrame {
	const Lens& lens;
	FilmCentre centre;
	std::vector<Surface>& surfaces;
};

struct SurfaceTile {
	std::uint32_t next;
	std::vector<std::uint32_t> pixels;
};

struct BlockPixel {
	int x;
	int y;
	bool inside;
};

SurfaceFrame& surfaceFrame(const Renderer& renderer) {
	return *static_cast<SurfaceFrame*>(renderer.drawing.gathered.get());
}

SurfaceTile& surfaceTile(const Worker& worker) {
	return *static_cast<SurfaceTile*>(worker.held.get());
}

Ray surfaceRay(const Lens& lens, const FilmCentre& centre, int x, int y) {
	return filmRay(lens, double(x) + centre.x, double(y) + centre.y);
}

int blocksAcross(const Tile& tile) {
	return (tile.width + surfaceBlockWidth - 1) / surfaceBlockWidth;
}

std::uint32_t placesOf(const Tile& tile) {
	const int down = (tile.height + surfaceBlockRows - 1) / surfaceBlockRows;
	return std::uint32_t(blocksAcross(tile) * down * surfaceBlockWidth * surfaceBlockRows);
}

BlockPixel blockPixel(const Tile& tile, std::uint32_t place) {
	const int lanes = surfaceBlockWidth * surfaceBlockRows;
	const int block = int(place) / lanes;
	const int lane  = int(place) % lanes;
	const int x     = block % blocksAcross(tile) * surfaceBlockWidth + lane % surfaceBlockWidth;
	const int y     = block / blocksAcross(tile) * surfaceBlockRows + lane / surfaceBlockWidth;
	return BlockPixel {
		.x      = tile.x + x,
		.y      = tile.y + y,
		.inside = x < tile.width && y < tile.height,
	};
}

void openedSurfaces(const Renderer&, Worker& worker) {
	if (!worker.held) {
		const SurfaceTile fresh = {
			.next   = 0,
			.pixels = std::vector<std::uint32_t>(std::size_t(worker.pool.capacity), 0),
		};

		worker.held = std::make_shared<SurfaceTile>(fresh);
	}
	surfaceTile(worker).next = 0;
}

void probedPixel(const Renderer& renderer, const BlockPixel& pixel, std::uint32_t slot, Worker& worker) {
	const SurfaceFrame& frame        = surfaceFrame(renderer);
	worker.pool.phases[slot]         = WalkPhase::Probing;
	worker.pool.kinds[slot]          = RayKind::Camera;
	worker.pool.rays[slot]           = surfaceRay(frame.lens, frame.centre, pixel.x, pixel.y);
	surfaceTile(worker).pixels[slot] = std::uint32_t(pixel.y) * std::uint32_t(frame.lens.width) + std::uint32_t(pixel.x);
}

bool probedNext(const Renderer& renderer, std::uint32_t slot, Worker& worker) {
	SurfaceTile& held          = surfaceTile(worker);
	const std::uint32_t places = placesOf(worker.tile);
	while (held.next < places) {
		const BlockPixel pixel = blockPixel(worker.tile, held.next);
		held.next += 1;
		if (pixel.inside) {
			probedPixel(renderer, pixel, slot, worker);
			return true;
		}
	}
	return false;
}

bool launchSurfaces(const Renderer& renderer, Worker& worker) {
	bool alive = false;
	for (std::uint32_t slot = 0; slot < std::uint32_t(worker.pool.capacity); ++slot) {
		alive = worker.pool.phases[slot] != WalkPhase::Free || probedNext(renderer, slot, worker) || alive;
	}
	return alive;
}

void foldedSurface(const Renderer& renderer, std::uint32_t slot, Worker& worker) {
	surfaceFrame(renderer).surfaces[surfaceTile(worker).pixels[slot]] = renderer.transport.hooks.cameraSurface(renderer, worker, slot);
	worker.pool.phases[slot]                                          = WalkPhase::Free;
	worker.pool.answered[slot]                                        = 0;
}

void foldSurfaces(const Renderer& renderer, Worker& worker) {
	const Pool& pool = worker.pool;
	for (std::uint32_t slot = 0; slot < std::uint32_t(pool.capacity); ++slot) {
		if (pool.answered[slot] != 0 && pool.phases[slot] == WalkPhase::Probing) {
			foldedSurface(renderer, slot, worker);
		}
	}
}

void placedSurfaces(Renderer&, const Worker&) {
}

Pass surfacePass() {
	return Pass {
		.opened = openedSurfaces,
		.launch = launchSurfaces,
		.fold   = foldSurfaces,
		.placed = placedSurfaces,
	};
}

std::vector<Surface> tracedSurfaces(Renderer& renderer) {
	const Lens& lens = renderer.step.lens;
	std::vector<Surface> surfaces(std::size_t(lens.width) * std::size_t(lens.height), missedSurface());

	const SurfaceFrame frame = {
		.lens     = lens,
		.centre   = filmCentre(renderer.step.film),
		.surfaces = surfaces,
	};

	passedTiles(renderer, surfacePass(), tilesOf(lens.width, lens.height, surfaceTileSide), std::make_shared<SurfaceFrame>(frame));
	return surfaces;
}

} // namespace kodanuki
