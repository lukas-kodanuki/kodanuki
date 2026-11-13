#pragma once

#include "core/batch.h"
#include "core/stopping.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

struct Plugin;

/**
 * How many samples of a pixel a slot launches at a time.
 */
constexpr int slotSamples = 8;

/**
 * The job of a slot that holds none.
 */
constexpr std::uint32_t noJob = 0xFFFFFFFFu;

/**
 * A run of samples for one pixel, continued from what it holds.
 */
struct PixelJob {
	/**
	 * The pixel's column.
	 */
	int x;
	/**
	 * The pixel's row.
	 */
	int y;
	/**
	 * The first sample's index in the pixel's whole run.
	 */
	int first;
	/**
	 * How many samples.
	 */
	int count;
	/**
	 * What the pixel's earlier samples add up to, empty for none.
	 */
	Estimate start;
	/**
	 * How many samples a batch holds, which the fold closes batches by.
	 */
	int batch;
	/**
	 * How many samples share a first vertex, one for samples alone. The
	 * job holds whole groups.
	 */
	int split;
	/**
	 * The first group's place in the pixel's run of groups.
	 */
	int group;
};

/**
 * One pixel in flight and the samples of it that are out.
 */
struct PixelSlot {
	/**
	 * The job it runs, or noJob where it holds none.
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
 * A tile's pixels as the pixel pass of one worker holds them, updated
 * in place (rule 46).
 *
 * A slot stands for slotSamples neighbouring walks of the worker's
 * pool: the walk of the slot's k-th sample in flight is the pool's
 * slot `slot * slotSamples + k`.
 */
struct PixelWork {
	/**
	 * The tile's jobs, one a pixel, by rows.
	 */
	std::vector<PixelJob> jobs;
	/**
	 * The pixels in flight.
	 */
	std::vector<PixelSlot> slots;
	/**
	 * The colour each walk ended with, kept until its slot folds.
	 */
	std::vector<Colour> colours;
	/**
	 * The part of each walk's colour that its group shares (the
	 * Transport's branchWalks), kept until its slot folds.
	 */
	std::vector<Colour> shared;
	/**
	 * Each job's estimate, set when the job is done.
	 */
	std::vector<Estimate> estimates;
	/**
	 * What the camera ray of each job's last sample met first, set when
	 * the job is done; missedSurface() for a job of no samples, and
	 * empty where the walks keep none (Renderer.drawing.recording).
	 */
	std::vector<Surface> firsts;
	/**
	 * The next job no slot has taken.
	 */
	std::uint32_t next;
};

/**
 * Return the first sample of one of a job's groups: its index, its
 * group's place and how many samples share its first vertex.
 *
 * @param job The job.
 * @param offset The sample's place in the job, the first of its group.
 */
PixelSample groupSample(const PixelJob& job, int offset);

/**
 * Return a slot that holds no job.
 */
PixelSlot idleSlot();
} // namespace kodanuki
