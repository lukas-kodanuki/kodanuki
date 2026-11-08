#include "extras/execution_correct_waves/wave.h"

#include "extras/execution_correct_waves/answer.h"

#include <algorithm>
#include <cstddef>
#include <memory>

namespace kodanuki {

void queuedWalk(Wave& wave, std::uint32_t token) {
	const Wanted wanted = wantedRay(wave.tracing, wave.walks[token]);

	const HeldRay held = {
		.ray   = wanted.ray,
		.token = token,
	};

	queued(wave.queues, wanted.kind, held);
}

void launched(Wave& wave, std::uint32_t slot) {
	PixelSlot& held     = wave.slots[slot];
	const PixelJob& job = wave.jobs[held.job];
	const int count     = std::min(slotSamples, job.first + job.count - held.launched);
	for (int k = 0; k < count; ++k) {
		const PixelSample sample = {
			.x     = job.x,
			.y     = job.y,
			.index = std::uint32_t(held.launched + k),
		};

		const std::uint32_t token = slot * std::uint32_t(slotSamples) + std::uint32_t(k);
		walkRestarted(wave.tracing, sample, wave.walks[token]);
		queuedWalk(wave, token);
	}
	held.launched += count;
	held.batch = count;
	held.owing = count;
}

void takenJob(Wave& wave, std::uint32_t slot) {
	while (wave.next < wave.jobs.size()) {
		const std::uint32_t job = wave.next;
		const PixelJob& taken   = wave.jobs[job];
		wave.next += 1;
		if (taken.count <= 0) {
			wave.estimates[job] = taken.start;
			continue;
		}
		wave.slots[slot] = PixelSlot {
			.job      = job,
			.estimate = taken.start,
			.launched = taken.first,
			.batch    = 0,
			.owing    = 0,
		};

		launched(wave, slot);
		return;
	}
	wave.slots[slot].job = noJob;
}

Recorded slotRecord(const Wave& wave, std::uint32_t slot) {
	const std::uint32_t token = slot * std::uint32_t(slotSamples) + std::uint32_t(wave.slots[slot].batch - 1);

	const Parts parts = {
		.total = wave.colours[token],
	};

	return Recorded {
		.parts   = parts,
		.surface = wave.walks[token].first,
	};
}

void foldedSlot(Wave& wave, std::uint32_t slot) {
	PixelSlot& held     = wave.slots[slot];
	const PixelJob& job = wave.jobs[held.job];
	for (int k = 0; k < held.batch; ++k) {
		held.estimate = foldedSample(held.estimate, wave.colours[slot * std::uint32_t(slotSamples) + std::uint32_t(k)], job.batch);
	}
	if (held.launched < job.first + job.count) {
		launched(wave, slot);
		return;
	}
	wave.estimates[held.job] = held.estimate;
	if (wave.tracing.recording) {
		wave.records[held.job] = slotRecord(wave, slot);
	}
	takenJob(wave, slot);
}

void answeredWalk(Wave& wave, std::uint32_t token, const Hit& hit) {
	const Answer answer = {
		.hit       = hit,
		.registers = wave.registers,
	};

	walkAnswered(wave.tracing, wave.walks[token], answer);
	const Walk& walk = wave.walks[token];
	if (walk.phase != WalkPhase::Done) {
		queuedWalk(wave, token);
		return;
	}
	wave.colours[token]      = colourOf(walk.path.wavelengths, walk.path.radiance);
	const std::uint32_t slot = token / std::uint32_t(slotSamples);
	wave.slots[slot].owing -= 1;
	if (wave.slots[slot].owing == 0) {
		foldedSlot(wave, slot);
	}
}

void answeredPacket(Wave& wave, const Packet& packet) {
	const PacketMet met = walkedPacket(wave.tracing.geometry, packet, wave.tricks);

	const PacketAnswer answer = {
		.met    = met,
		.tricks = wave.tricks,
	};

	const std::array<Hit, packetWidth> hits = packetHits(wave.tracing.geometry, packet, answer);
	for (int lane = 0; lane < packetWidth; ++lane) {
		if (((packet.present >> lane) & 1) != 0) {
			answeredWalk(wave, packet.tokens[std::size_t(lane)], hits[std::size_t(lane)]);
		}
	}
}

void runWave(Wave& wave) {
	while (!drained(wave.queues)) {
		answeredPacket(wave, nextPacket(wave.queues));
	}
}

PixelSlot idleSlot() {
	return PixelSlot {
		.job      = noJob,
		.estimate = emptyEstimate(),
		.launched = 0,
		.batch    = 0,
		.owing    = 0,
	};
}

std::unique_ptr<Walk[]>& spareWalks() {
	thread_local std::unique_ptr<Walk[]> spare;
	return spare;
}

std::unique_ptr<Walk[]> reusedWalks() {
	std::unique_ptr<Walk[]>& spare = spareWalks();
	if (spare == nullptr) {
		return std::make_unique_for_overwrite<Walk[]>(std::size_t(pathsInFlight));
	}
	return std::move(spare);
}

void sparedWalks(Wave& wave) {
	spareWalks() = std::move(wave.walks);
}

Wave startedWave(const Tracing& tracing, const std::vector<PixelJob>& jobs, const WaveTricks& tricks) {
	const std::size_t slots = std::min(std::size_t(pathsInFlight / slotSamples), jobs.size());
	const std::size_t walks = slots * std::size_t(slotSamples);

	Wave wave = {
		.tracing   = tracing,
		.jobs      = jobs,
		.walks     = reusedWalks(),
		.colours   = std::vector<Colour>(walks),
		.slots     = std::vector<PixelSlot>(slots, idleSlot()),
		.queues    = emptyQueues(std::size_t(pathsInFlight / 8)),
		.estimates = std::vector<Estimate>(jobs.size()),
		.records   = tracing.recording ? std::vector<Recorded>(jobs.size(), emptyRecord()) : std::vector<Recorded> {},
		.next      = 0,
		.registers = {},
		.tricks    = tricks,
	};

	for (std::uint32_t slot = 0; slot < slots; ++slot) {
		takenJob(wave, slot);
	}
	return wave;
}

std::vector<Estimate> jobsInWaves(const Tracing& tracing, const std::vector<PixelJob>& jobs) {
	Wave wave = startedWave(tracing, jobs, waveTricks());
	runWave(wave);
	sparedWalks(wave);
	return wave.estimates;
}

RecordedJobs recordedInWaves(const Tracing& tracing, const std::vector<PixelJob>& jobs) {
	Wave wave = startedWave(tracing, jobs, waveTricks());
	runWave(wave);
	sparedWalks(wave);
	return RecordedJobs {
		.estimates = wave.estimates,
		.records   = wave.records,
	};
}

}
