#include "extras/execution_correct_waves/surface_wave.h"

#include "extras/execution_correct_waves/answer.h"
#include "transport/camera.h"
#include "walk/span.h"

#include <algorithm>
#include <bit>
#include <cstddef>

namespace kodanuki {

Packet emptyCameraPacket(int octant) {
	return Packet {
		.rays       = {},
		.tokens     = {},
		.present    = 0,
		.descending = octant,
		.kind       = RayKind::Camera,
	};
}

Packet withLane(const Packet& packet, const Ray& ray, std::uint32_t token) {
	Packet added           = packet;
	const std::size_t lane = std::size_t(std::popcount(packet.present));
	added.rays[lane]       = ray;
	added.tokens[lane]     = token;
	added.present          = std::uint8_t(packet.present | (1u << lane));
	return added;
}

std::array<Packet, 8> blockPackets(const SurfaceBand& band, int x) {
	std::array<Packet, 8> packets;
	for (int octant = 0; octant < 8; ++octant) {
		packets[std::size_t(octant)] = emptyCameraPacket(octant);
	}
	const int right = std::min(x + surfaceBlockWidth, band.lens.width);
	for (int y = band.y; y < band.y + band.rows; ++y) {
		for (int column = x; column < right; ++column) {
			const Ray ray             = surfaceRay(band, column, y);
			const std::size_t octant  = std::size_t(descendingOf(ray));
			const std::uint32_t token = std::uint32_t((y - band.y) * band.lens.width + column);
			packets[octant]           = withLane(packets[octant], ray, token);
		}
	}
	return packets;
}

std::array<Surface, packetWidth> laneSurfaces(const SurfaceWave& wave, const Packet& packet) {
	const PacketMet met = walkedPacket(wave.band.tracing.geometry, packet, wave.tricks);

	const PacketAnswer answer = {
		.met    = met,
		.tricks = wave.tricks,
	};

	const std::array<Hit, packetWidth> hits = packetHits(wave.band.tracing.geometry, packet, answer);
	std::array<Surface, packetWidth> surfaces;
	for (std::size_t lane = 0; lane < std::size_t(packetWidth); ++lane) {
		const CameraHit camera = {
			.ray       = packet.rays[lane],
			.hit       = hits[lane],
			.registers = wave.registers,
		};

		const bool present = ((packet.present >> lane) & 1) != 0;
		surfaces[lane]     = present ? cameraSurface(wave.band.tracing, camera) : missedSurface();
	}
	return surfaces;
}

void surfacesPlaced(const Packet& packet, const std::array<Surface, packetWidth>& lanes, std::vector<Surface>& surfaces) {
	const std::size_t present = std::size_t(std::popcount(packet.present));
	for (std::size_t lane = 0; lane < present; ++lane) {
		surfaces[packet.tokens[lane]] = lanes[lane];
	}
}

std::vector<Surface> bandInWaves(const SurfaceBand& band) {
	std::vector<double> registers;

	const SurfaceWave wave = {
		.band      = band,
		.tricks    = waveTricks(),
		.registers = registers,
	};

	std::vector<Surface> surfaces(std::size_t(band.lens.width) * std::size_t(band.rows), missedSurface());
	for (int x = 0; x < band.lens.width; x += surfaceBlockWidth) {
		for (const Packet& packet : blockPackets(band, x)) {
			if (packet.present != 0) {
				surfacesPlaced(packet, laneSurfaces(wave, packet), surfaces);
			}
		}
	}
	return surfaces;
}

}
