#pragma once

#include "extras/execution_correct_waves/packet.h"
#include "extras/execution_correct_waves/queue.h"
#include "frame/execution.h"
#include "transport/trace.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kodanuki {

/**
 * The most paths a wave keeps in flight.
 */
constexpr int pathsInFlight = 512;

/**
 * How many samples of a pixel a slot launches at a time.
 */
constexpr int slotSamples = 8;

/**
 * The job of a slot that has none left to take.
 */
constexpr std::uint32_t noJob = 0xFFFFFFFFu;

/**
 * One pixel in flight and the samples of it that are out.
 */
struct PixelSlot {
	/**
	 * The job it runs, or noJob once every job has been taken.
	 */
	std::uint32_t job;
	/**
	 * What the pixel's samples folded so far add up to.
	 */
	Estimate estimate;
	/**
	 * The index past the last sample launched.
	 */
	int launched;
	/**
	 * How many samples the last launch sent out.
	 */
	int batch;
	/**
	 * How many of them are still out.
	 */
	int owing;
};

/**
 * A tile's jobs in flight.
 *
 * Its walks, colours, slots and queues are updated in place (rule 46).
 * Every ray is answered through walkAnswered() and every pixel folded
 * through foldedSample(), so the wave changes only the order the rays
 * are traced in, never what any path computes.
 */
struct Wave {
	/**
	 * What the transport reads.
	 */
	const Tracing& tracing;
	/**
	 * The jobs.
	 */
	const std::vector<PixelJob>& jobs;
	/**
	 * The walks, slotSamples a slot.
	 *
	 * They are left unwritten until a slot launches them, since
	 * walkRestarted() writes all a walk reads.
	 */
	std::unique_ptr<Walk[]> walks;
	/**
	 * The colour each walk ended with, kept until its slot folds.
	 */
	std::vector<Colour> colours;
	/**
	 * The slots.
	 */
	std::vector<PixelSlot> slots;
	/**
	 * The rays waiting.
	 */
	RayQueues queues;
	/**
	 * Each job's estimate, set when it is done.
	 */
	std::vector<Estimate> estimates;
	/**
	 * What each job's last sample left, kept only where the tracing
	 * records (Tracing.recording); empty otherwise.
	 */
	std::vector<Recorded> records;
	/**
	 * The next job no slot has taken.
	 */
	std::uint32_t next;
	/**
	 * The registers the patterns run in.
	 */
	std::vector<double> registers;
	/**
	 * The tricks of the packet walk.
	 */
	WaveTricks tricks;
};

/**
 * Run jobs in waves: the Waves member.
 *
 * Each slot takes the next job and launches slotSamples of its samples
 * at a time. When the last of them ends, their colours are folded in
 * index order and the next are launched; when the job is done, the
 * slot takes the next job. Packets are taken off the queues, walked and
 * answered until no ray waits.
 *
 * @param tracing What the transport reads.
 * @param jobs The tile's pixels, by rows.
 */
std::vector<Estimate> jobsInWaves(const Tracing& tracing, const std::vector<PixelJob>& jobs);

/**
 * Run jobs in waves, as jobsInWaves() does, with a tracing that records,
 * and keep what each job's last sample left: the Waves member of
 * recordedJobs().
 *
 * @param tracing What the transport reads, which records.
 * @param jobs The tile's pixels, by rows.
 */
RecordedJobs recordedInWaves(const Tracing& tracing, const std::vector<PixelJob>& jobs);

/**
 * Run a wave in place until no ray waits: take a packet off the queues,
 * walk it, and answer its lanes in lane order.
 *
 * @param wave The wave, as startedWave() makes it.
 */
void runWave(Wave& wave);

/**
 * Return a wave over a tile's jobs with every slot that has a job
 * launched.
 *
 * @param tracing What the transport reads.
 * @param jobs The tile's pixels, by rows.
 * @param tricks The tricks of the packet walk.
 */
Wave startedWave(const Tracing& tracing, const std::vector<PixelJob>& jobs, const WaveTricks& tricks);

}
