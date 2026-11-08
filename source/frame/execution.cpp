#include "frame/execution.h"

#include "extras/execution_correct_waves/light_wave.h"
#include "extras/execution_correct_waves/surface_wave.h"
#include "extras/execution_correct_waves/wave.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>

namespace kodanuki {

Estimate jobOneAtATime(const Tracing& tracing, const PixelJob& job) {
	Estimate estimate = job.start;
	for (int k = 0; k < job.count; ++k) {
		const PixelSample sample = {
			.x     = job.x,
			.y     = job.y,
			.index = std::uint32_t(job.first + k),
		};

		const Traced traced = tracePath(tracing, sample);
		estimate            = foldedSample(estimate, colourOf(traced.wavelengths, traced.radiance), job.batch);
	}
	return estimate;
}

std::vector<Estimate> jobsOneAtATime(const Tracing& tracing, const std::vector<PixelJob>& jobs) {
	std::vector<Estimate> estimates;
	estimates.reserve(jobs.size());
	for (const PixelJob& job : jobs) {
		estimates.push_back(jobOneAtATime(tracing, job));
	}
	return estimates;
}

struct JobRecord {
	Estimate estimate;
	Recorded record;
};

JobRecord jobRecorded(const Tracing& tracing, const PixelJob& job) {
	JobRecord done = {
		.estimate = job.start,
		.record   = emptyRecord(),
	};

	for (int k = 0; k < job.count; ++k) {
		const PixelSample sample = {
			.x     = job.x,
			.y     = job.y,
			.index = std::uint32_t(job.first + k),
		};

		done.record   = recordedPath(tracing, sample);
		done.estimate = foldedSample(done.estimate, done.record.parts.total, job.batch);
	}
	return done;
}

RecordedJobs recordedOneAtATime(const Tracing& tracing, const std::vector<PixelJob>& jobs) {
	RecordedJobs recorded = {
		.estimates = {},
		.records   = {},
	};

	for (const PixelJob& job : jobs) {
		const JobRecord done = jobRecorded(tracing, job);
		recorded.estimates.push_back(done.estimate);
		recorded.records.push_back(done.record);
	}
	return recorded;
}

RecordedJobs recordedJobs(const Execution& execution, const std::vector<PixelJob>& jobs) {
	switch (execution.member) {
		case ExecutionMember::OneAtATime: return recordedOneAtATime(execution.tracing, jobs);
		case ExecutionMember::Waves: return recordedInWaves(execution.tracing, jobs);
	}

	return recordedOneAtATime(execution.tracing, jobs);
}

std::vector<Estimate> executedJobs(const Execution& execution, const std::vector<PixelJob>& jobs) {
	switch (execution.member) {
		case ExecutionMember::OneAtATime: return jobsOneAtATime(execution.tracing, jobs);
		case ExecutionMember::Waves: return jobsInWaves(execution.tracing, jobs);
	}

	return jobsOneAtATime(execution.tracing, jobs);
}

std::vector<Splat> lightJobOneAtATime(const Tracing& tracing, const LightJob& job) {
	std::vector<double> registers;
	std::vector<Splat> splats;
	for (std::uint32_t k = 0; k < job.count; ++k) {
		LightWalk walk = passWalkStarted(tracing, job.first + k);
		while (walk.phase != LightPhase::Done) {
			const Hit hit = intersect(tracing.geometry, lightRayWanted(tracing, walk).ray);

			const Answer answer = {
				.hit       = hit,
				.registers = registers,
			};

			lightWalkAnswered(tracing, walk, answer);
		}
		splats.insert(splats.end(), walk.landing.splats.begin(), walk.landing.splats.begin() + walk.landing.count);
	}
	return splats;
}

std::vector<Splat> executedLightJob(const Execution& execution, const LightJob& job) {
	switch (execution.member) {
		case ExecutionMember::OneAtATime: return lightJobOneAtATime(execution.tracing, job);
		case ExecutionMember::Waves: return lightJobInWaves(execution.tracing, job);
	}

	return lightJobOneAtATime(execution.tracing, job);
}

void inThreads(int count, const std::function<void()>& work) {
	std::vector<std::thread> threads;
	for (int k = 0; k < count; ++k) {
		threads.emplace_back(work);
	}
	for (std::thread& thread : threads) {
		thread.join();
	}
}

Ray surfaceRay(const SurfaceBand& band, int x, int y) {
	return filmRay(band.lens, double(x) + band.centre.x, double(y) + band.centre.y);
}

std::vector<Surface> bandOneAtATime(const SurfaceBand& band) {
	std::vector<double> registers;
	std::vector<Surface> surfaces;
	surfaces.reserve(std::size_t(band.lens.width) * std::size_t(band.rows));
	for (int y = band.y; y < band.y + band.rows; ++y) {
		for (int x = 0; x < band.lens.width; ++x) {
			const Ray ray = surfaceRay(band, x, y);
			const Hit hit = intersect(band.tracing.geometry, ray);

			const CameraHit met = {
				.ray       = ray,
				.hit       = hit,
				.registers = registers,
			};

			surfaces.push_back(cameraSurface(band.tracing, met));
		}
	}
	return surfaces;
}

std::vector<Surface> bandSurfaces(ExecutionMember member, const SurfaceBand& band) {
	switch (member) {
		case ExecutionMember::OneAtATime: return bandOneAtATime(band);
		case ExecutionMember::Waves: return bandInWaves(band);
	}

	return bandOneAtATime(band);
}

std::vector<Surface> surfacesOf(const SurfaceQuery& query) {
	const Lens& lens        = query.lens;
	const FilmCentre centre = filmCentre(query.film);
	std::vector<Surface> surfaces(std::size_t(lens.width) * std::size_t(lens.height), missedSurface());
	std::atomic<int> next = 0;
	const auto work       = [&]() {
		for (int y = next.fetch_add(surfaceBandRows); y < lens.height; y = next.fetch_add(surfaceBandRows)) {
			const SurfaceBand band = {
				.tracing = query.tracing,
				.lens    = lens,
				.centre  = centre,
				.y       = y,
				.rows    = std::min(surfaceBandRows, lens.height - y),
			};

			const std::vector<Surface> traced = bandSurfaces(query.member, band);
			std::copy(traced.begin(), traced.end(), surfaces.begin() + std::ptrdiff_t(lens.width) * y);
		}
	};

	inThreads(std::max(query.threads, 1), work);
	return surfaces;
}

}
