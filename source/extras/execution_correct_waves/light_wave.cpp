#include "extras/execution_correct_waves/light_wave.h"

#include "extras/execution_correct_waves/answer.h"
#include "extras/execution_correct_waves/wave.h"

#include <algorithm>
#include <cstddef>

namespace kodanuki {

void queuedLightWalk(LightWave& wave, std::uint32_t slot) {
	const Wanted wanted = lightRayWanted(wave.tracing, wave.walks[slot]);

	const HeldRay held = {
		.ray   = wanted.ray,
		.token = slot,
	};

	queued(wave.queues, wanted.kind, held);
}

void takenLightPath(LightWave& wave, std::uint32_t slot) {
	while (wave.next < wave.job.count) {
		const std::uint32_t path = wave.next;
		wave.next += 1;
		const LightWalk walk = passWalkStarted(wave.tracing, wave.job.first + path);
		if (walk.phase == LightPhase::Done) {
			wave.landings[path] = walk.landing;
			continue;
		}
		wave.walks[slot]   = walk;
		wave.running[slot] = path;
		queuedLightWalk(wave, slot);
		return;
	}
}

void answeredLightWalk(LightWave& wave, std::uint32_t slot, const Hit& hit) {
	const Answer answer = {
		.hit       = hit,
		.registers = wave.registers,
	};

	lightWalkAnswered(wave.tracing, wave.walks[slot], answer);
	const LightWalk& walk = wave.walks[slot];
	if (walk.phase != LightPhase::Done) {
		queuedLightWalk(wave, slot);
		return;
	}
	wave.landings[wave.running[slot]] = walk.landing;
	takenLightPath(wave, slot);
}

void answeredLightPacket(LightWave& wave, const Packet& packet) {
	const PacketMet met = walkedPacket(wave.tracing.geometry, packet, wave.tricks);

	const PacketAnswer answer = {
		.met    = met,
		.tricks = wave.tricks,
	};

	const std::array<Hit, packetWidth> hits = packetHits(wave.tracing.geometry, packet, answer);
	for (int lane = 0; lane < packetWidth; ++lane) {
		if (((packet.present >> lane) & 1) != 0) {
			answeredLightWalk(wave, packet.tokens[std::size_t(lane)], hits[std::size_t(lane)]);
		}
	}
}

struct SpareLight {
	std::vector<LightWalk> walks;
	std::vector<FilmLanding> landings;
};

SpareLight& spareLight() {
	thread_local SpareLight spare = {
		.walks    = {},
		.landings = {},
	};

	return spare;
}

std::vector<LightWalk> reusedLightWalks(std::size_t slots) {
	std::vector<LightWalk> walks = std::move(spareLight().walks);
	walks.clear();
	walks.resize(slots);
	return walks;
}

std::vector<FilmLanding> reusedLandings(std::size_t count, const FilmLanding& none) {
	std::vector<FilmLanding> landings = std::move(spareLight().landings);
	landings.assign(count, none);
	return landings;
}

void sparedLight(LightWave& wave) {
	SpareLight& spare = spareLight();
	spare.walks       = std::move(wave.walks);
	spare.landings    = std::move(wave.landings);
}

LightWave startedLightWave(const Tracing& tracing, const LightJob& job) {
	const std::size_t slots = std::min(std::size_t(pathsInFlight), std::size_t(job.count));

	const FilmLanding none = {
		.splats = {},
		.count  = 0,
	};

	LightWave wave = {
		.tracing   = tracing,
		.job       = job,
		.walks     = reusedLightWalks(slots),
		.running   = std::vector<std::uint32_t>(slots, 0),
		.landings  = reusedLandings(job.count, none),
		.queues    = emptyQueues(std::size_t(pathsInFlight / 8)),
		.next      = 0,
		.registers = {},
		.tricks    = waveTricks(),
	};

	for (std::uint32_t slot = 0; slot < slots; ++slot) {
		takenLightPath(wave, slot);
	}
	return wave;
}

std::vector<Splat> lightJobInWaves(const Tracing& tracing, const LightJob& job) {
	LightWave wave = startedLightWave(tracing, job);
	while (!drained(wave.queues)) {
		answeredLightPacket(wave, nextPacket(wave.queues));
	}

	std::vector<Splat> splats;
	for (const FilmLanding& landing : wave.landings) {
		splats.insert(splats.end(), landing.splats.begin(), landing.splats.begin() + landing.count);
	}
	sparedLight(wave);
	return splats;
}

}
