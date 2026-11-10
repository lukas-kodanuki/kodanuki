#pragma once

#include "frame/stopping.h"
#include "transport/light_walk.h"
#include "transport/trace.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace kodanuki {

/**
 * A way of running the paths of a tile.
 *
 * Every member runs the same paths. Each path is a pure function of its
 * pixel, its sample index and the frame, and every member folds a
 * pixel's samples by foldedSample() in index order, so the frame is the
 * same to the byte whichever member runs and on any thread count.
 */
enum class ExecutionMember : std::uint8_t {
	/**
	 * One path at a time: each ray is traced by the scalar walk as soon
	 * as the path asks for it.
	 */
	OneAtATime,
	/**
	 * Waves of paths whose rays wait in queues by kind and octant, traced
	 * eight at a time by the packet walk (extras/execution_correct_waves).
	 */
	Waves,
};

/**
 * How many rows a band of a G-buffer holds: two, so the packets of the
 * Waves member trace blocks of four by two neighbouring pixels.
 */
constexpr int surfaceBandRows = 2;

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
 * A run of the light pass's paths, named by their indices across the
 * whole pass.
 */
struct LightJob {
	/**
	 * The first path's index.
	 */
	std::uint32_t first;
	/**
	 * How many paths.
	 */
	std::uint32_t count;
};

/**
 * What a member runs the jobs with.
 */
struct Execution {
	/**
	 * The member.
	 */
	ExecutionMember member;
	/**
	 * What the transport reads.
	 */
	const Tracing& tracing;
};

/**
 * What a tile's jobs leave for the frames of a sequence.
 */
struct RecordedJobs {
	/**
	 * Each job's estimate, in the order of the jobs.
	 */
	std::vector<Estimate> estimates;
	/**
	 * What each job's last sample left, in the order of the jobs;
	 * emptyRecord() for a job of no samples.
	 */
	std::vector<Recorded> records;
};

/**
 * What a G-buffer is traced for: each drawn pixel's first surface.
 */
struct SurfaceQuery {
	/**
	 * The member.
	 */
	ExecutionMember member;
	/**
	 * What the surfaces are shaded with.
	 */
	const Tracing& tracing;
	/**
	 * The lens at the drawn size: one ray from the lens's middle for each
	 * pixel (surfaceRay()).
	 */
	const Lens& lens;
	/**
	 * How the drawn pixels cover the shown ones: each pixel's ray passes
	 * through the middle of the shown pixel its phase aims at.
	 */
	FilmPlan film;
	/**
	 * How many threads trace the rows.
	 */
	int threads;
};

/**
 * A band of whole rows of a G-buffer, which one thread traces at a time.
 */
struct SurfaceBand {
	/**
	 * What the surfaces are shaded with.
	 */
	const Tracing& tracing;
	/**
	 * The lens at the drawn size.
	 */
	const Lens& lens;
	/**
	 * Where in each pixel its ray passes (filmCentre()).
	 */
	FilmCentre centre;
	/**
	 * The first row.
	 */
	int y;
	/**
	 * How many rows.
	 */
	int rows;
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
 * Run a tile's jobs and return each pixel's estimate: the point's one
 * dispatch.
 *
 * The estimates come back in the order of the jobs. Each is the job's
 * start with the job's samples folded in by foldedSample(), in index
 * order.
 *
 * @param execution The member and the transport.
 * @param jobs The tile's pixels, by rows.
 */
std::vector<Estimate> executedJobs(const Execution& execution, const std::vector<PixelJob>& jobs);

/**
 * Run a tile's jobs with a tracing that records (Tracing.recording) and
 * return each pixel's estimate and what its last sample left: the
 * point's dispatch for the frames of a sequence.
 *
 * The estimates are those executedJobs() returns, and the records are
 * the same to the byte whichever member runs.
 *
 * @param execution The member and the transport, which records.
 * @param jobs The tile's pixels, by rows.
 */
RecordedJobs recordedJobs(const Execution& execution, const std::vector<PixelJob>& jobs);

/**
 * Run jobs one path at a time and keep each job's last record: the
 * OneAtATime member of recordedJobs().
 *
 * Each sample is traced by recordedPath() and folded at once.
 *
 * @param tracing What the transport reads, which records.
 * @param jobs The pixels.
 */
RecordedJobs recordedOneAtATime(const Tracing& tracing, const std::vector<PixelJob>& jobs);

/**
 * Run jobs one path at a time: the OneAtATime member.
 *
 * Each sample is traced by tracePath() and folded at once.
 *
 * @param tracing What the transport reads.
 * @param jobs The pixels.
 */
std::vector<Estimate> jobsOneAtATime(const Tracing& tracing, const std::vector<PixelJob>& jobs);

/**
 * Run a job's light paths and return what they brought: the point's
 * dispatch for light paths.
 *
 * The splats come back by path index, and each path's in the order of
 * its surfaces, so the frame folds them in one order whichever member
 * runs and on any thread count.
 *
 * @param execution The member and the transport.
 * @param job The paths.
 */
std::vector<Splat> executedLightJob(const Execution& execution, const LightJob& job);

/**
 * Run light paths one at a time, each ray traced by intersect() as it
 * is asked: the OneAtATime member.
 *
 * @param tracing What the transport reads.
 * @param job The paths.
 */
std::vector<Splat> lightJobOneAtATime(const Tracing& tracing, const LightJob& job);

/**
 * Return the ray a band's pixel traces its surface by: the filmRay()
 * through the point the band's centre names inside the pixel, at a
 * factor of 1 the pixel's pixelRay().
 *
 * @param band The rows.
 * @param x The pixel's column.
 * @param y The pixel's row.
 */
Ray surfaceRay(const SurfaceBand& band, int x, int y);

/**
 * Trace each drawn pixel's first surface and return the G-buffer: the
 * point's dispatch for the surfaces a frame shows.
 *
 * The surfaces come back by rows from the top left, at the drawn size,
 * each cameraSurface() of the pixel's surfaceRay(): the ray from the
 * lens's middle through the middle of the shown pixel the film plan's
 * phase aims the drawn pixel at. The vector is the same to the byte
 * whichever member runs and on any thread count. The threads take bands
 * of surfaceBandRows rows in turn.
 *
 * @param query The member, the transport, the drawn lens, the film plan
 *        and the threads.
 */
std::vector<Surface> surfacesOf(const SurfaceQuery& query);

/**
 * Trace a band's surfaces one ray at a time by intersect(): the
 * OneAtATime member of surfacesOf().
 *
 * The surfaces come back by rows.
 *
 * @param band The rows.
 */
std::vector<Surface> bandOneAtATime(const SurfaceBand& band);

/**
 * Run the same work on count threads at once and wait for them all.
 *
 * @param count How many threads.
 * @param work What each thread runs, which takes its share by itself.
 */
void inThreads(int count, const std::function<void()>& work);

}
