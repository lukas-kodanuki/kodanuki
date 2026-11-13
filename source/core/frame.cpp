#include "core/frame.h"

#include "core/plugin.h"
#include "core/render.h"
#include "core/step.h"
#include "core/surfaces.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace kodanuki {

Estimate sumStart(const PixelSum& sum) {
	Estimate start = emptyEstimate();
	start.total    = sum.total;
	start.taken    = sum.taken;
	return start;
}

std::size_t pixelAt(const Renderer& renderer, int x, int y) {
	return std::size_t(y) * std::size_t(renderer.step.lens.width) + std::size_t(x);
}

PixelJob trialJob(const FrameDrawing& drawing, int x, int y) {
	return PixelJob {
		.x     = x,
		.y     = y,
		.first = int(drawing.offset),
		.count = drawing.trial.samples,
		.start = emptyEstimate(),
		.batch = drawing.pixels.batch,
		.split = drawing.trial.split,
		.group = int(drawing.offset),
	};
}

PixelJob countJob(const Renderer& renderer, int x, int y) {
	const FrameDrawing& drawing = renderer.drawing;
	const Trial& trial          = drawing.trial;

	const CountQuery query = {
		.x     = x,
		.y     = y,
		.trial = trial,
	};

	const int split = splitFor(drawing.pixels, query);
	return PixelJob {
		.x     = x,
		.y     = y,
		.first = int(drawing.offset) + trial.samples,
		.count = (samplesFor(drawing.pixels, query) - trial.samples) / split * split,
		.start = sumStart(drawing.estimates[pixelAt(renderer, x, y)]),
		.batch = drawing.pixels.batch,
		.split = split,
		.group = int(drawing.offset) + trial.samples / trial.split,
	};
}

PixelJob fixedJob(const Renderer& renderer, int x, int y) {
	const FrameDrawing& drawing = renderer.drawing;
	const PixelSum& sum         = drawing.estimates[pixelAt(renderer, x, y)];
	const Stopping& stopping    = drawing.pixels;
	const int samples           = stopping.samples;
	const int split             = stopping.split > 0 && stopping.split <= samples ? stopping.split : 1;
	const int first             = int(drawing.offset) + sum.taken;
	return PixelJob {
		.x     = x,
		.y     = y,
		.first = first,
		.count = samples / split * split,
		.start = sumStart(sum),
		.batch = stopping.batch,
		.split = split,
		.group = first / split,
	};
}

PixelJob pixelJob(const Renderer& renderer, int x, int y) {
	if (renderer.drawing.trial.samples <= 0) {
		return fixedJob(renderer, x, y);
	}
	return renderer.drawing.round == 0 ? trialJob(renderer.drawing, x, y) : countJob(renderer, x, y);
}

std::size_t pixelOf(const Tile& tile, std::size_t k, int width) {
	const int x = tile.x + int(k) % tile.width;
	const int y = tile.y + int(k) / tile.width;
	return std::size_t(y) * std::size_t(width) + std::size_t(x);
}

Preparations framePreparations(Renderer& renderer) {
	return Preparations {
		.plugins = renderer.preparations,
		.states  = renderer.history.states,
	};
}

void judgedPixel(Renderer& renderer, const Estimate& estimate, std::size_t pixel) {
	const Preparations prepared = framePreparations(renderer);
	FrameDrawing& drawing       = renderer.drawing;

	const TrialPixel judged = {
		.estimate = estimate,
		.prepared = preparedLight(prepared, pixel),
		.spread   = preparedSpread(prepared, pixel),
	};

	drawing.trial.wanted[pixel]  = wantedSamples(drawing.pixels, judged);
	drawing.trial.spreads[pixel] = splitSpread(estimate, drawing.pixels.batch, drawing.trial.split);
}

Colour pixelColour(const PixelSum& estimate, const Preparations& prepared, std::size_t pixel) {
	const Colour mean = estimate.taken > 0 ? estimate.total * (1.0 / estimate.taken) : Colour { 0, 0, 0 };
	return mean + preparedLight(prepared, pixel);
}

void placedPixel(Renderer& renderer, const Worker& worker, std::size_t k) {
	FrameDrawing& drawing    = renderer.drawing;
	const std::size_t pixel  = pixelOf(worker.tile, k, renderer.step.lens.width);
	const Estimate& estimate = worker.pixels.estimates[k];
	drawing.estimates[pixel] = PixelSum { estimate.total, estimate.taken };
	if (drawing.trial.samples > 0 && drawing.round == 0) {
		judgedPixel(renderer, estimate, pixel);
	}
	if (!drawing.firsts.empty() && worker.pixels.jobs[k].count > 0) {
		drawing.firsts[pixel] = worker.pixels.firsts[k];
	}
}

void openedPixels(const Renderer& renderer, Worker& worker) {
	PixelWork& pixels = worker.pixels;
	pixels.jobs.clear();
	for (int y = worker.tile.y; y < worker.tile.y + worker.tile.height; ++y) {
		for (int x = worker.tile.x; x < worker.tile.x + worker.tile.width; ++x) {
			pixels.jobs.push_back(pixelJob(renderer, x, y));
		}
	}
	pixels.estimates.assign(pixels.jobs.size(), emptyEstimate());
	pixels.firsts.assign(renderer.drawing.recording ? pixels.jobs.size() : 0, missedSurface());
	pixels.next = 0;
}

void launchedSlot(const Renderer& renderer, std::uint32_t slot, Worker& worker) {
	PixelSlot& held     = worker.pixels.slots[slot];
	const PixelJob& job = worker.pixels.jobs[held.job];
	const int count     = std::min(slotSamples, job.first + job.count - held.launched);
	for (int k = 0; k < count; k += job.split) {
		const std::uint32_t token  = slot * std::uint32_t(slotSamples) + std::uint32_t(k);
		worker.pool.samples[token] = groupSample(job, held.launched - job.first + k);
		renderer.transport.hooks.startWalk(renderer, worker, token);
	}
	held.launched += count;
	held.batch = count;
	held.owing = count;
}

void takenPixel(const Renderer& renderer, std::uint32_t slot, Worker& worker) {
	PixelWork& pixels = worker.pixels;
	while (pixels.next < pixels.jobs.size()) {
		const std::uint32_t job = pixels.next;
		const PixelJob& taken   = pixels.jobs[job];
		pixels.next += 1;
		if (taken.count <= 0) {
			pixels.estimates[job] = taken.start;
			continue;
		}
		pixels.slots[slot] = PixelSlot {
			.job      = job,
			.estimate = taken.start,
			.launched = taken.first,
			.batch    = 0,
			.owing    = 0,
		};

		launchedSlot(renderer, slot, worker);
		return;
	}
}

void branchedWalks(const Renderer& renderer, std::uint32_t token, Worker& worker) {
	const std::uint32_t branches = worker.pool.samples[token].branches;
	std::fill_n(worker.pixels.shared.begin() + token, branches, Colour { 0, 0, 0 });
	worker.pixels.shared[token] = renderer.transport.hooks.branchWalks(renderer, worker, token);
}

void branchedGroups(const Renderer& renderer, Worker& worker) {
	for (std::uint32_t token = 0; token < std::uint32_t(worker.pool.capacity); ++token) {
		if (worker.pool.phases[token] == WalkPhase::Branching) {
			branchedWalks(renderer, token, worker);
		}
	}
}

bool launchedPixel(const Renderer& renderer, std::uint32_t slot, Worker& worker) {
	const PixelSlot& held = worker.pixels.slots[slot];
	if (held.job == noJob) {
		takenPixel(renderer, slot, worker);
		return held.job != noJob;
	}
	if (held.owing == 0) {
		launchedSlot(renderer, slot, worker);
	}
	return true;
}

bool launchPixels(const Renderer& renderer, Worker& worker) {
	branchedGroups(renderer, worker);
	bool alive = false;
	for (std::uint32_t slot = 0; slot < worker.pixels.slots.size(); ++slot) {
		alive = launchedPixel(renderer, slot, worker) || alive;
	}
	return alive;
}

void foldedPixel(const Renderer& renderer, std::uint32_t slot, Worker& worker) {
	PixelWork& pixels   = worker.pixels;
	PixelSlot& held     = pixels.slots[slot];
	const PixelJob& job = pixels.jobs[held.job];
	for (int k = 0; k < held.batch; ++k) {
		const std::uint32_t token = slot * std::uint32_t(slotSamples) + std::uint32_t(k);

		const Sampled drawn = {
			.colour = pixels.colours[token],
			.shared = pixels.shared[token],
		};

		held.estimate = foldedSample(held.estimate, drawn, job.batch);
	}
	if (held.launched < job.first + job.count) {
		return;
	}
	if (!pixels.firsts.empty()) {
		pixels.firsts[held.job] = renderer.transport.hooks.firstSurface(worker, slot * std::uint32_t(slotSamples) + std::uint32_t(held.batch - job.split));
	}
	pixels.estimates[held.job] = held.estimate;
	held.job                   = noJob;
}

void settledWalks(const Renderer& renderer, std::uint32_t token, int count, Worker& worker) {
	const std::uint32_t slot = token / std::uint32_t(slotSamples);
	worker.pixels.slots[slot].owing -= count;
	if (worker.pixels.slots[slot].owing == 0) {
		foldedPixel(renderer, slot, worker);
	}
}

void endedStem(const Renderer& renderer, std::uint32_t token, Worker& worker) {
	PixelWork& pixels            = worker.pixels;
	const std::uint32_t branches = worker.pool.samples[token].branches;
	std::fill_n(pixels.colours.begin() + token, branches, Colour { 0, 0, 0 });
	std::fill_n(pixels.shared.begin() + token, branches, Colour { 0, 0, 0 });
	pixels.colours[token] = renderer.transport.hooks.walkColour(worker, token);
	pixels.shared[token]  = pixels.colours[token];
	settledWalks(renderer, token, int(branches), worker);
}

void endedSlot(const Renderer& renderer, std::uint32_t token, Worker& worker) {
	worker.pool.phases[token] = WalkPhase::Free;
	if (!renderer.transport.hooks.walkBranched(worker, token)) {
		endedStem(renderer, token, worker);
		return;
	}
	worker.pixels.colours[token] = renderer.transport.hooks.walkColour(worker, token);
	settledWalks(renderer, token, 1, worker);
}

void foldPixels(const Renderer& renderer, Worker& worker) {
	for (std::uint32_t token = 0; token < std::uint32_t(worker.pool.capacity); ++token) {
		if (worker.pool.phases[token] == WalkPhase::Done) {
			endedSlot(renderer, token, worker);
		}
	}
}

void placedPixels(Renderer& renderer, const Worker& worker) {
	for (std::size_t k = 0; k < worker.pixels.estimates.size(); ++k) {
		placedPixel(renderer, worker, k);
	}
}

Pass pixelPass() {
	return Pass {
		.opened = openedPixels,
		.launch = launchPixels,
		.fold   = foldPixels,
		.placed = placedPixels,
	};
}

int fittedSplit(int split, int batch) {
	if (split > 0 && batch % split != 0) {
		std::fprintf(stderr, "kodanuki: a split of %d needs batches of a multiple of it, not %d\n", split, batch);
		std::exit(1);
	}
	return split;
}

Stopping frameStopping(const Renderer& renderer) {
	const Shot& shot     = renderer.file.shot;
	const Render& render = shot.render;
	const Effort effort  = effortFor(render.tolerance);
	const int batch      = render.batch > 0 ? render.batch : effort.batch;
	return Stopping {
		.plugin    = &renderer.stopping,
		.samples   = render.samples > 0 ? render.samples : effort.samples,
		.batch     = batch,
		.tolerance = double(render.tolerance),
		.grade     = shot.grade,
		.split     = fittedSplit(render.split, batch),
	};
}

Stopping pixelStopping(const Stopping& stopping, int perFrame) {
	Stopping pixels = stopping;
	if (perFrame > 0) {
		pixels.plugin  = nullptr;
		pixels.samples = perFrame;
	}
	return pixels;
}

std::shared_ptr<void> frameTransport(Renderer& renderer, FilmPlan film, bool recording) {
	const TransportFrame frame = {
		.world     = stateOf(renderer, renderer.transport.name),
		.geometry  = renderer.world->geometry,
		.looks     = renderer.file.looks,
		.lighting  = renderer.file.lighting,
		.shot      = renderer.file.shot,
		.lens      = renderer.step.lens,
		.film      = film,
		.recording = recording,
		.states    = renderer.history.states,
	};

	return renderer.transport.hooks.openFrame(frame);
}

void preparedBy(Renderer& renderer, const Plugin& plugin) {
	renderer.drawing.transported = frameTransport(renderer, stillFilm(), false);
	const std::uint32_t index    = pluginIndex(plugin.name);
	const double begun           = frameStepBegun(renderer.observing, LoopStep::Preparations, index, 0);
	plugin.hooks.prepareFrame(renderer);
	frameStepEnded(renderer.observing, LoopStep::Preparations, index, 0, begun);
	renderer.drawing.transported = nullptr;
}

void preparedFrame(Renderer& renderer) {
	for (const Plugin& plugin : renderer.preparations) {
		if (preparesAgain(plugin, renderer.step.change)) {
			preparedBy(renderer, plugin);
		}
	}
}

std::size_t lensPixels(const Lens& lens) {
	return std::size_t(lens.width) * std::size_t(lens.height);
}

bool continuesEstimates(const Renderer& renderer) {
	return renderer.step.change == Change::None && renderer.history.estimates.size() == lensPixels(renderer.step.lens);
}

Trial openedTrial(const Stopping& stopping, const Lens& lens) {
	const int samples        = trialSamples(stopping);
	const std::size_t pixels = samples > 0 ? lensPixels(lens) : 0;
	return Trial {
		.samples = samples,
		.width   = lens.width,
		.height  = lens.height,
		.wanted  = std::vector<float>(pixels, 0.0f),
		.split   = samples > 0 ? trialSplit(stopping) : 1,
		.spreads = std::vector<SplitSpread>(pixels, SplitSpread { 0, 0 }),
	};
}

Trial noTrial() {
	return Trial {
		.samples = 0,
		.width   = 0,
		.height  = 0,
		.wanted  = {},
		.split   = 1,
		.spreads = {},
	};
}

Stopping noStopping() {
	const Grade grade = {
		.exposure = 1,
		.curve    = Curve::Filmic,
	};

	return Stopping {
		.plugin    = nullptr,
		.samples   = 0,
		.batch     = 1,
		.tolerance = 0,
		.grade     = grade,
		.split     = 0,
	};
}

FrameDrawing idleDrawing() {
	return FrameDrawing {
		.stopping    = noStopping(),
		.pixels      = noStopping(),
		.transported = nullptr,
		.recording   = false,
		.trial       = noTrial(),
		.round       = 0,
		.offset      = 0,
		.estimates   = {},
		.firsts      = {},
		.tiles       = {},
		.gathered    = nullptr,
	};
}

void passedTiles(Renderer& renderer, const Pass& pass, std::vector<Tile> tiles, std::shared_ptr<void> gathered) {
	renderer.drawing.tiles    = std::move(tiles);
	renderer.drawing.gathered = std::move(gathered);
	onWorkers(renderer, pass);
	renderer.drawing.tiles.clear();
	renderer.drawing.gathered = nullptr;
}

int roundsOwed(const Stopping& stopping, bool continued) {
	int rounds = 0;
	while (nextRound(stopping, rounds, continued)) {
		rounds += 1;
	}
	return rounds;
}

std::uint32_t frameTiles(const Renderer& renderer) {
	const Lens& lens      = renderer.step.lens;
	const Stopping pixels = pixelStopping(frameStopping(renderer), renderer.options.perFrame);
	const int rounds      = roundsOwed(pixels, continuesEstimates(renderer));
	return std::uint32_t(tilesOf(lens.width, lens.height, 16).size()) * std::uint32_t(rounds);
}

std::uint32_t sampleOffset(const Renderer& renderer) {
	const FrameStep& step = renderer.step;
	return (step.index - step.still) * std::uint32_t(renderer.drawing.pixels.samples);
}

int drawnPixels(Renderer& renderer, bool continued) {
	FrameDrawing& drawing         = renderer.drawing;
	const Lens& lens              = renderer.step.lens;
	const std::vector<Tile> tiles = tilesOf(lens.width, lens.height, 16);
	drawing.trial                 = openedTrial(drawing.pixels, lens);
	drawing.offset                = sampleOffset(renderer);
	const int rounds              = roundsOwed(drawing.pixels, continued);
	drawing.firsts.assign(drawing.recording && rounds > 0 ? drawing.estimates.size() : 0, missedSurface());
	for (int round = 0; round < rounds; ++round) {
		drawing.round = round;
		passedTiles(renderer, pixelPass(), tiles, nullptr);
	}
	drawing.trial = noTrial();
	return rounds;
}

std::vector<PixelSum> startedEstimates(Renderer& renderer, bool continued) {
	std::vector<PixelSum> estimates = std::move(renderer.history.estimates);
	if (!continued) {
		estimates.assign(lensPixels(renderer.step.lens), PixelSum { { 0, 0, 0 }, 0 });
	}
	return estimates;
}

Parts frameLight(const PixelSum& before, const PixelSum& after) {
	const int drawn = after.taken - before.taken;
	return Parts {
		.total = drawn > 0 ? (after.total + before.total * -1.0) * (1.0 / drawn) : Colour { 0, 0, 0 },
	};
}

std::vector<Parts> frameLights(const std::vector<PixelSum>& before, const std::vector<PixelSum>& after) {
	const PixelSum none = { { 0, 0, 0 }, 0 };
	std::vector<Parts> parts;
	parts.reserve(after.size());
	for (std::size_t pixel = 0; pixel < after.size(); ++pixel) {
		parts.push_back(frameLight(before.empty() ? none : before[pixel], after[pixel]));
	}
	return parts;
}

FrameRecord frameRecordOf(const Lens& lens, const std::vector<PixelSum>& before, FrameDrawing& drawing) {
	const bool records = !drawing.firsts.empty();
	return FrameRecord {
		.width    = lens.width,
		.height   = lens.height,
		.parts    = records ? frameLights(before, drawing.estimates) : std::vector<Parts> {},
		.surfaces = std::move(drawing.firsts),
	};
}

bool keepsRecord(const FrameDrawing& drawing, int rounds, const FrameRecord& last) {
	return drawing.recording && rounds == 0 && last.parts.size() == drawing.estimates.size();
}

std::vector<Surface> shownSurfaces(Renderer& renderer) {
	if (!readsSurfaces(renderer.reconstructions)) {
		return {};
	}
	return tracedSurfaces(renderer);
}

double meanSamples(const std::vector<PixelSum>& estimates) {
	double total = 0;
	for (const PixelSum& sum : estimates) {
		total += sum.taken;
	}
	return estimates.empty() ? 0 : total / double(estimates.size());
}

std::vector<PixelSum> openedPixelFrame(Renderer& renderer, bool continued) {
	FrameDrawing& drawing              = renderer.drawing;
	const double begun                 = frameStepBegun(renderer.observing, LoopStep::Preparations, corePlugin, 0);
	drawing.recording                  = readsRecords(renderer.reconstructions);
	drawing.transported                = frameTransport(renderer, renderer.step.film, drawing.recording);
	drawing.estimates                  = startedEstimates(renderer, continued);
	const bool differs                 = drawing.recording && continued && nextRound(drawing.pixels, 0, continued);
	const std::vector<PixelSum> before = differs ? drawing.estimates : std::vector<PixelSum> {};
	frameStepEnded(renderer.observing, LoopStep::Preparations, corePlugin, 0, begun);
	return before;
}

Image shownImage(Renderer& renderer, const FrameRecord& record) {
	const FrameStep& step  = renderer.step;
	const Lens shown       = shownLens(step.lens, step.film);
	const Lens shownBefore = shownLens(step.before, step.film);
	const Change moves     = joinedChange(joinedChange(Change::Moved, Change::Lights), joinedChange(Change::Looks, Change::Shapes));

	const double surfacedAt             = frameStepBegun(renderer.observing, LoopStep::Surfaces, corePlugin, 0);
	const std::vector<Surface> surfaces = shownSurfaces(renderer);
	frameStepEnded(renderer.observing, LoopStep::Surfaces, corePlugin, 0, surfacedAt);

	const ShownFrame frame = {
		.width     = shown.width,
		.height    = shown.height,
		.lens      = shown,
		.before    = shownBefore,
		.film      = step.film,
		.moved     = changedBy(step.change, moves),
		.restarted = changedBy(step.change, Change::Cut),
		.recounted = renderer.frame.recounted,
		.still     = step.still,
		.record    = record,
		.estimates = renderer.drawing.estimates,
		.surfaces  = surfaces,
		.threads   = renderer.options.choices.threads,
	};

	const ReconstructionQuery shownQuery = {
		.frame    = frame,
		.prepared = framePreparations(renderer),
	};

	const double shownAt = frameStepBegun(renderer.observing, LoopStep::Reconstruction, corePlugin, 0);
	Image image          = reconstructedFrame(renderer.reconstructions, shownQuery, renderer.history.states);
	frameStepEnded(renderer.observing, LoopStep::Reconstruction, corePlugin, 0, shownAt);
	return image;
}

void drawnFrame(Renderer& renderer) {
	FrameDrawing& drawing = renderer.drawing;
	drawing.stopping      = frameStopping(renderer);
	drawing.pixels        = pixelStopping(drawing.stopping, renderer.options.perFrame);
	preparedFrame(renderer);
	const bool continued               = continuesEstimates(renderer);
	const std::vector<PixelSum> before = openedPixelFrame(renderer, continued);
	const int rounds                   = drawnPixels(renderer, continued);

	const bool keeps   = keepsRecord(drawing, rounds, renderer.history.record);
	FrameRecord record = keeps ? std::move(renderer.history.record) : frameRecordOf(renderer.step.lens, before, drawing);
	Image image        = shownImage(renderer, record);

	const double kept          = frameStepBegun(renderer.observing, LoopStep::Keeping, corePlugin, 0);
	const double samples       = meanSamples(drawing.estimates);
	renderer.history.record    = std::move(record);
	renderer.history.estimates = std::move(drawing.estimates);
	drawing.transported        = nullptr;
	frameStepEnded(renderer.observing, LoopStep::Keeping, corePlugin, 0, kept);

	renderer.drawn = Drawn {
		.image   = std::move(image),
		.samples = samples,
		.rounds  = rounds,
	};
}

} // namespace kodanuki
