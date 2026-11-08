#pragma once

#include "extras/execution_correct_waves/packet.h"
#include "extras/execution_correct_waves/queue.h"
#include "frame/execution.h"
#include "transport/light_walk.h"

#include <cstdint>
#include <vector>

namespace kodanuki {

/**
 * A light job's paths in flight.
 *
 * Its walks, landings and queues are updated in place (rule 46). Every
 * ray is answered through lightWalkAnswered(), and each path's landing is
 * kept by its index in the job, so the wave changes only the order the
 * rays are traced in, never what any path computes.
 */
struct LightWave {
	/**
	 * What the transport reads.
	 */
	const Tracing& tracing;
	/**
	 * The job.
	 */
	LightJob job;
	/**
	 * The walks in flight, one a slot.
	 */
	std::vector<LightWalk> walks;
	/**
	 * The index in the job of the path each slot runs.
	 */
	std::vector<std::uint32_t> running;
	/**
	 * Each path's landing, by its index in the job, set when it is done.
	 */
	std::vector<FilmLanding> landings;
	/**
	 * The rays waiting.
	 */
	RayQueues queues;
	/**
	 * The index in the job of the next path no slot has taken.
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
 * Run a light job in waves: the Waves member for light paths.
 *
 * Each slot takes the next path and keeps it in flight until it is done,
 * then takes the next. Packets are taken off the queues, walked and
 * answered until no ray waits. The splats come back by path index.
 *
 * @param tracing What the transport reads.
 * @param job The paths.
 */
std::vector<Splat> lightJobInWaves(const Tracing& tracing, const LightJob& job);

}
