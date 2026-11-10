#include "frame/frame.h"

#include "light/light.h"
#include "material/material.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace kodanuki {

struct NormalsRow {
	const Geometry& geometry;
	const Lens& lens;
	int y;
};

std::vector<float> normalsRow(const NormalsRow& row) {
	std::vector<float> values(std::size_t(row.lens.width) * 3, 0.0f);
	for (int x = 0; x < row.lens.width; ++x) {
		const Hit hit  = intersect(row.geometry, pixelRay(row.lens, x, row.y));
		const bool met = hit.kind != HitKind::Missed;
		for (int i = 0; i < 3; ++i) {
			values[std::size_t(x) * 3 + std::size_t(i)] = met ? float(math::component(hit.normal, i)) * 0.5f + 0.5f : 0.0f;
		}
	}
	return values;
}

Image normalsImage(const Geometry& geometry, const Lens& lens, int threads) {
	Image image           = blankImage(lens.width, lens.height);
	std::atomic<int> next = 0;
	const auto work       = [&]() {
		for (int y = next++; y < lens.height; y = next++) {
			const NormalsRow row = {
				.geometry = geometry,
				.lens     = lens,
				.y        = y,
			};

			const std::vector<float> values = normalsRow(row);
			std::copy(values.begin(), values.end(), image.rgb.begin() + std::ptrdiff_t(values.size()) * y);
		}
	};

	inThreads(threads, work);
	return image;
}

std::vector<Tile> tilesOf(int width, int height, int size) {
	std::vector<Tile> tiles;
	for (int y = 0; y < height; y += size) {
		for (int x = 0; x < width; x += size) {
			const Tile tile = {
				.x      = x,
				.y      = y,
				.width  = std::min(size, width - x),
				.height = std::min(size, height - y),
			};

			tiles.push_back(tile);
		}
	}
	return tiles;
}

enum class FramePass : std::uint8_t {
	Trial,
	Count,
	Step,
};

struct TileWork {
	const Tracing& tracing;
	const Stopping& stopping;
	const Prepared& prepared;
	const Trial& trial;
	const std::vector<Estimate>& earlier;
	std::uint32_t index;
	ExecutionMember execution;
	FramePass pass;
	Progress& progress;
};

Estimate trialStart(const Trial& trial, std::size_t pixel) {
	Estimate start = emptyEstimate();
	start.total    = trial.totals[pixel];
	start.taken    = trial.samples;
	return start;
}

PixelJob trialJob(const TileWork& work, int x, int y) {
	return PixelJob {
		.x     = x,
		.y     = y,
		.first = 0,
		.count = work.trial.samples,
		.start = emptyEstimate(),
		.batch = work.stopping.batch,
		.split = work.trial.split,
		.group = 0,
	};
}

PixelJob countJob(const TileWork& work, int x, int y) {
	const Trial& trial = work.trial;

	const CountQuery query = {
		.x     = x,
		.y     = y,
		.trial = trial,
	};

	const std::size_t pixel = std::size_t(y) * std::size_t(trial.width) + std::size_t(x);
	const bool tried        = trial.samples > 0;
	const int split         = splitFor(work.stopping, query);
	return PixelJob {
		.x     = x,
		.y     = y,
		.first = trial.samples,
		.count = (samplesFor(work.stopping, query) - trial.samples) / split * split,
		.start = tried ? trialStart(trial, pixel) : emptyEstimate(),
		.batch = work.stopping.batch,
		.split = split,
		.group = trial.samples / trial.split,
	};
}

PixelJob stepJob(const TileWork& work, int x, int y) {
	const std::size_t pixel = std::size_t(y) * std::size_t(work.tracing.lens.width) + std::size_t(x);
	return PixelJob {
		.x     = x,
		.y     = y,
		.first = int(work.index),
		.count = 1,
		.start = work.earlier.empty() ? emptyEstimate() : work.earlier[pixel],
		.batch = work.stopping.batch,
		.split = 1,
		.group = int(work.index),
	};
}

PixelJob pixelJob(const TileWork& work, int x, int y) {
	switch (work.pass) {
		case FramePass::Trial: return trialJob(work, x, y);
		case FramePass::Count: return countJob(work, x, y);
		case FramePass::Step: return stepJob(work, x, y);
	}

	return countJob(work, x, y);
}

std::size_t pixelOf(const Tile& tile, std::size_t k, int width) {
	const int x = tile.x + int(k) % tile.width;
	const int y = tile.y + int(k) / tile.width;
	return std::size_t(y) * std::size_t(width) + std::size_t(x);
}

TileResult emptyResult(const Tile& tile) {
	return TileResult {
		.tile      = tile,
		.rgb       = {},
		.wanted    = {},
		.totals    = {},
		.spreads   = {},
		.estimates = {},
		.records   = {},
		.samples   = 0,
	};
}

void judgedPixel(const TileWork& work, const Estimate& estimate, std::size_t pixel, TileResult& result) {
	const TrialPixel judged = {
		.estimate = estimate,
		.prepared = preparedLight(work.prepared, pixel),
		.spread   = preparedSpread(work.prepared, pixel),
	};

	result.wanted.push_back(wantedSamples(work.stopping, judged));
	result.totals.push_back(estimate.total);
	result.spreads.push_back(splitSpread(estimate, work.stopping.batch, work.trial.split));
}

Colour pixelColour(const Estimate& estimate, const Prepared& prepared, std::size_t pixel) {
	const Colour mean = estimate.taken > 0 ? estimate.total * (1.0 / estimate.taken) : Colour { 0, 0, 0 };
	return mean + preparedLight(prepared, pixel);
}

void colouredPixel(const TileWork& work, const Estimate& estimate, std::size_t pixel, TileResult& result) {
	const Colour value = pixelColour(estimate, work.prepared, pixel);
	result.rgb.insert(result.rgb.end(), { float(value.red), float(value.green), float(value.blue) });
}

TileResult keptResult(const TileWork& work, const Tile& tile, const std::vector<Estimate>& estimates) {
	TileResult result = emptyResult(tile);
	for (std::size_t k = 0; k < estimates.size(); ++k) {
		const std::size_t pixel = pixelOf(tile, k, work.tracing.lens.width);
		result.samples += estimates[k].taken;
		if (work.pass == FramePass::Trial) {
			judgedPixel(work, estimates[k], pixel, result);
			continue;
		}
		colouredPixel(work, estimates[k], pixel, result);
	}
	return result;
}

TileResult steppedTile(const Execution& execution, const Tile& tile, const std::vector<PixelJob>& jobs) {
	RecordedJobs recorded = recordedJobs(execution, jobs);
	TileResult result     = emptyResult(tile);
	result.estimates      = std::move(recorded.estimates);
	result.records        = std::move(recorded.records);
	result.samples        = double(jobs.size());
	return result;
}

TileResult tileResult(const TileWork& work, const Tile& tile) {
	std::vector<PixelJob> jobs;
	for (int y = tile.y; y < tile.y + tile.height; ++y) {
		for (int x = tile.x; x < tile.x + tile.width; ++x) {
			jobs.push_back(pixelJob(work, x, y));
		}
	}

	const Execution execution = {
		.member  = work.execution,
		.tracing = work.tracing,
	};

	if (work.pass == FramePass::Step) {
		return steppedTile(execution, tile, jobs);
	}
	return keptResult(work, tile, executedJobs(execution, jobs));
}

Image placedTiles(const std::vector<TileResult>& results, int width, int height) {
	Image placed = blankImage(width, height);
	for (const TileResult& result : results) {
		for (std::size_t k = 0; k * 3 < result.rgb.size(); ++k) {
			const std::size_t pixel = pixelOf(result.tile, k, width);
			std::copy_n(result.rgb.begin() + std::ptrdiff_t(k * 3), 3, placed.rgb.begin() + std::ptrdiff_t(pixel * 3));
		}
	}
	return placed;
}

int fittedSplit(int split, int batch) {
	if (split > 0 && batch % split != 0) {
		std::fprintf(stderr, "kodanuki: a split of %d needs batches of a multiple of it, not %d\n", split, batch);
		std::exit(1);
	}
	return split;
}

Stopping frameStopping(const FrameInput& input) {
	const Render& render = input.shot.render;
	const Effort effort  = effortFor(render.tolerance);
	const int batch      = render.batch > 0 ? render.batch : effort.batch;
	return Stopping {
		.member    = input.stopping,
		.samples   = render.samples > 0 ? render.samples : effort.samples,
		.batch     = batch,
		.tolerance = double(render.tolerance),
		.grade     = input.shot.grade,
		.split     = fittedSplit(render.split, batch),
	};
}

FrameBasis frameBasis(const FrameInput& input) {
	return FrameBasis {
		.materials = featuredMaterials(input.features, materialsOf(input.looks, input.smoothGlass), input.smoothGlass),
		.lights    = lightsOf(input.lighting),
		.stopping  = frameStopping(input),
	};
}

Tracing frameTracing(const FrameInput& input, const FrameBasis& basis, const Prepared& prepared) {
	return Tracing {
		.geometry   = input.geometry,
		.scene      = input.geometry.scene,
		.looks      = input.looks,
		.materials  = basis.materials,
		.lights     = basis.lights,
		.lens       = input.lens,
		.sampling   = prepared.sampling,
		.strategies = prepared.strategies,
		.features   = input.features,
		.limits     = transportLimits(input.shot.render),
		.seed       = std::uint64_t(input.shot.render.seed),
		.film       = stillFilm(),
		.recording  = false,
	};
}

struct PreparationStep {
	const FrameInput& input;
	const FrameBasis& basis;
	PreparationMember member;
};

void preparedOnce(const PreparationStep& step, Prepared& prepared) {
	const Tracing tracing = frameTracing(step.input, step.basis, prepared);

	const PreparationQuery query = {
		.tracing   = tracing,
		.stopping  = step.basis.stopping,
		.execution = step.input.execution,
		.threads   = step.input.threads,
	};

	preparedBy(step.member, query, prepared);
}

Prepared preparedFrame(const FrameInput& input, const FrameBasis& basis) {
	Prepared prepared = input.chosen;
	for (const PreparationMember member : input.preparations) {
		const PreparationStep step = {
			.input  = input,
			.basis  = basis,
			.member = member,
		};

		preparedOnce(step, prepared);
	}
	return prepared;
}

Prepared preparedFrom(const FrameInput& input, const FrameBasis& basis, const PreparationRun& run) {
	Prepared prepared = run.start;
	for (const PreparationMember member : input.preparations) {
		if (preparationReads(member) != run.reads) {
			continue;
		}

		const PreparationStep step = {
			.input  = input,
			.basis  = basis,
			.member = member,
		};

		preparedOnce(step, prepared);
	}
	return prepared;
}

std::vector<TileResult> tileResults(const TileWork& work, const std::vector<Tile>& tiles, int threads) {
	std::vector<TileResult> results(tiles.size());
	std::atomic<std::size_t> next = 0;
	const auto run                = [&]() {
		for (std::size_t k = next++; k < tiles.size(); k = next++) {
			results[k] = tileResult(work, tiles[k]);
			work.progress.finished.fetch_add(1, std::memory_order_relaxed);
		}
	};

	inThreads(threads, run);
	return results;
}

void judgedTile(const TileResult& result, Trial& trial) {
	for (std::size_t k = 0; k < result.wanted.size(); ++k) {
		const std::size_t pixel = pixelOf(result.tile, k, trial.width);
		trial.wanted[pixel]     = result.wanted[k];
		trial.totals[pixel]     = result.totals[k];
		trial.spreads[pixel]    = result.spreads[k];
	}
}

Trial noTrial(const Lens& lens, int samples) {
	return Trial {
		.samples = samples,
		.width   = lens.width,
		.height  = lens.height,
		.wanted  = {},
		.totals  = {},
		.split   = 1,
		.spreads = {},
	};
}

Trial judgedTrial(const std::vector<TileResult>& results, const Trial& drawn) {
	const std::size_t pixels = std::size_t(drawn.width) * std::size_t(drawn.height);
	Trial trial              = drawn;
	trial.wanted.assign(pixels, 0.0f);
	trial.totals.assign(pixels, Colour { 0, 0, 0 });
	trial.spreads.assign(pixels, SplitSpread { 0, 0 });
	for (const TileResult& result : results) {
		judgedTile(result, trial);
	}
	return trial;
}

Trial frameTrial(const TiledRun& run, const std::vector<Tile>& tiles) {
	Trial drawn = noTrial(run.tracing.lens, trialSamples(run.stopping));
	if (drawn.samples <= 0) {
		return drawn;
	}
	drawn.split = trialSplit(run.stopping);

	const std::vector<Estimate> none = {};

	const TileWork work = {
		.tracing   = run.tracing,
		.stopping  = run.stopping,
		.prepared  = run.prepared,
		.trial     = drawn,
		.earlier   = none,
		.index     = 0,
		.execution = run.execution,
		.pass      = FramePass::Trial,
		.progress  = run.progress,
	};

	return judgedTrial(tileResults(work, tiles, run.threads), drawn);
}

int passesOf(const Stopping& stopping) {
	return trialSamples(stopping) > 0 ? 2 : 1;
}

std::vector<TileResult> tiledPass(const TiledRun& run) {
	const std::vector<Tile> tiles = tilesOf(run.tracing.lens.width, run.tracing.lens.height, 16);
	run.progress.total.fetch_add(int(tiles.size()) * passesOf(run.stopping), std::memory_order_relaxed);
	const Trial trial                = frameTrial(run, tiles);
	const std::vector<Estimate> none = {};

	const TileWork work = {
		.tracing   = run.tracing,
		.stopping  = run.stopping,
		.prepared  = run.prepared,
		.trial     = trial,
		.earlier   = none,
		.index     = 0,
		.execution = run.execution,
		.pass      = FramePass::Count,
		.progress  = run.progress,
	};

	return tileResults(work, tiles, run.threads);
}

void placedStep(const TileResult& result, int width, SteppedPass& pass) {
	for (std::size_t k = 0; k < result.estimates.size(); ++k) {
		const std::size_t pixel = pixelOf(result.tile, k, width);
		pass.estimates[pixel]   = result.estimates[k];
		pass.records[pixel]     = result.records[k];
	}
}

SteppedPass steppedPass(const SteppedRun& run) {
	const Lens& lens              = run.tracing.lens;
	const std::vector<Tile> tiles = tilesOf(lens.width, lens.height, 16);
	const std::size_t pixels      = std::size_t(lens.width) * std::size_t(lens.height);
	const Trial trial             = noTrial(lens, 0);
	run.progress.total.fetch_add(int(tiles.size()), std::memory_order_relaxed);

	const TileWork work = {
		.tracing   = run.tracing,
		.stopping  = run.stopping,
		.prepared  = run.prepared,
		.trial     = trial,
		.earlier   = run.estimates,
		.index     = run.index,
		.execution = run.execution,
		.pass      = FramePass::Step,
		.progress  = run.progress,
	};

	SteppedPass pass = {
		.estimates = std::vector<Estimate>(pixels, emptyEstimate()),
		.records   = std::vector<Recorded>(pixels, emptyRecord()),
	};

	for (const TileResult& result : tileResults(work, tiles, run.threads)) {
		placedStep(result, lens.width, pass);
	}
	return pass;
}

double meanSamples(const std::vector<TileResult>& results) {
	double total  = 0;
	double pixels = 0;
	for (const TileResult& result : results) {
		total += result.samples;
		pixels += double(result.tile.width) * double(result.tile.height);
	}
	return pixels > 0 ? total / pixels : 0;
}

Rendered renderFrame(const FrameInput& input) {
	const FrameBasis basis  = frameBasis(input);
	const Prepared prepared = preparedFrame(input, basis);
	const Tracing tracing   = frameTracing(input, basis, prepared);

	const TiledRun run = {
		.tracing   = tracing,
		.stopping  = basis.stopping,
		.prepared  = prepared,
		.execution = input.execution,
		.threads   = input.threads,
		.progress  = input.progress,
	};

	const std::vector<TileResult> results = tiledPass(run);
	return Rendered {
		.image   = placedTiles(results, input.lens.width, input.lens.height),
		.samples = meanSamples(results),
	};
}

Progress freshProgress() {
	return Progress {
		.finished = 0,
		.total    = 0,
	};
}

}
