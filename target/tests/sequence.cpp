#include "frame/sequence.h"
#include "check.h"
#include "extras/reconstruction_biased_upsample/upsample.h"
#include "frame/execution.h"
#include "frame/frame.h"
#include "frame/run.h"
#include "frame/shown.h"
#include "transport/camera.h"
#include "transport/feature.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace kodanuki {

struct SequenceScene {
	const SceneFile& file;
	const Geometry& geometry;
	const Features& features;
};

bool sameBytes(const void* a, const void* b, std::size_t size) {
	return std::memcmp(a, b, size) == 0;
}

bool sameColour(const Colour& a, const Colour& b) {
	return sameBytes(&a, &b, sizeof(Colour));
}

bool sameEstimate(const Estimate& a, const Estimate& b) {
	const bool sums = sameColour(a.total, b.total) && sameColour(a.batch, b.batch) && sameColour(a.means, b.means) && sameColour(a.squares, b.squares);
	return sums && a.taken == b.taken && a.batches == b.batches;
}

bool sameFirstSurface(const Surface& a, const Surface& b) {
	const bool lengths = sameBytes(&a.distance, &b.distance, sizeof(float)) && sameBytes(&a.onward, &b.onward, sizeof(float));
	const bool looks   = sameColour(a.albedo, b.albedo) && sameBytes(&a.roughness, &b.roughness, sizeof(float));
	return lengths && looks && sameBytes(&a.normal, &b.normal, sizeof(math::Vector)) && a.leaf == b.leaf && a.material == b.material;
}

bool sameSampleRecord(const Recorded& a, const Recorded& b) {
	return sameColour(a.parts.total, b.parts.total) && sameFirstSurface(a.surface, b.surface);
}

bool sameEstimates(const std::vector<Estimate>& a, const std::vector<Estimate>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t k = 0; k < a.size(); ++k) {
		if (!sameEstimate(a[k], b[k])) {
			return false;
		}
	}
	return true;
}

bool sameRecords(const std::vector<Recorded>& a, const std::vector<Recorded>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t k = 0; k < a.size(); ++k) {
		if (!sameSampleRecord(a[k], b[k])) {
			return false;
		}
	}
	return true;
}

int sequenceCount(int x, int y) {
	return (x * 7 + y) % 5 == 0 ? 0 : 1 + x % 2;
}

std::vector<PixelJob> sequenceJobs(const Lens& lens, int batch) {
	std::vector<PixelJob> jobs;
	for (int y = 0; y < lens.height; ++y) {
		for (int x = 0; x < lens.width; ++x) {
			const PixelJob job = {
				.x     = x,
				.y     = y,
				.first = (x + y) % 3,
				.count = sequenceCount(x, y),
				.start = emptyEstimate(),
				.batch = batch,
				.split = 1,
				.group = (x + y) % 3,
			};

			jobs.push_back(job);
		}
	}
	return jobs;
}

struct JobsRun {
	ExecutionMember execution;
	bool recording;
};

struct JobsDrawn {
	RecordedJobs recorded;
	std::vector<Estimate> executed;
};

JobsDrawn drawnJobs(const SequenceScene& scene, const JobsRun& run) {
	const Prepared chosen                     = defaultPrepared();
	const std::vector<PreparationMember> none = {};
	const Lens lens                           = lensOf(scene.file.shot.camera, scene.file.shot.render);
	Progress progress                         = freshProgress();

	const FrameInput input = {
		.geometry     = scene.geometry,
		.looks        = scene.file.looks,
		.lighting     = scene.file.lighting,
		.shot         = scene.file.shot,
		.lens         = lens,
		.chosen       = chosen,
		.preparations = none,
		.features     = scene.features,
		.stopping     = StoppingMember::FixedCount,
		.smoothGlass  = SmoothGlass::Exact,
		.execution    = run.execution,
		.threads      = 1,
		.progress     = progress,
	};

	const FrameBasis basis  = frameBasis(input);
	const Prepared prepared = preparedFrame(input, basis);
	Tracing tracing         = frameTracing(input, basis, prepared);
	tracing.recording       = run.recording;

	const Execution execution = {
		.member  = run.execution,
		.tracing = tracing,
	};

	const std::vector<PixelJob> jobs = sequenceJobs(lens, basis.stopping.batch);
	return JobsDrawn {
		.recorded = recordedJobs(execution, jobs),
		.executed = executedJobs(execution, jobs),
	};
}

int metSurfaces(const std::vector<Recorded>& records) {
	int met = 0;
	for (const Recorded& record : records) {
		const bool hit = record.surface.leaf != noLeaf && std::isfinite(record.surface.distance) && record.surface.distance > 0;
		met += hit ? 1 : 0;
	}
	return met;
}

bool recordFits(const Recorded& record, const Estimate& estimate, int count) {
	if (count == 0) {
		return record.surface.leaf == noLeaf && estimate.taken == 0;
	}
	return count != 1 || sameColour(record.parts.total, estimate.total);
}

bool recordsFit(const RecordedJobs& recorded, int width) {
	for (std::size_t k = 0; k < recorded.records.size(); ++k) {
		const int x = int(k) % width;
		const int y = int(k) / width;
		if (!recordFits(recorded.records[k], recorded.estimates[k], sequenceCount(x, y))) {
			return false;
		}
	}
	return true;
}

void checkRecords(const char* path, const SequenceScene& scene) {
	const JobsDrawn one   = drawnJobs(scene, JobsRun { ExecutionMember::OneAtATime, true });
	const JobsDrawn waves = drawnJobs(scene, JobsRun { ExecutionMember::Waves, true });
	const JobsDrawn still = drawnJobs(scene, JobsRun { ExecutionMember::Waves, false });
	const int met         = metSurfaces(one.recorded.records);
	const bool same       = sameRecords(one.recorded.records, waves.recorded.records);
	std::printf("sequence %s: records of waves %s one at a time, %d of %zu pixels met a surface\n", path, same ? "equal" : "differ from", met, one.recorded.records.size());
	check(same && sameEstimates(one.recorded.estimates, waves.recorded.estimates), "waves record each pixel's last sample as one path at a time does, to the byte");
	check(sameEstimates(one.recorded.estimates, still.executed) && sameEstimates(waves.recorded.estimates, waves.executed), "recording leaves every estimate as a frame that records nothing draws it");
	check(met > 0 && recordsFit(one.recorded, scene.file.shot.render.width), "a record keeps its sample's colour and the first surface its camera ray met, and a job of no samples a miss");
}

constexpr int stillFrames = 6;

struct SequenceSetup {
	Shot shot;
	Lens lens;
	Prepared chosen;
	std::vector<PreparationMember> preparations;
};

SequenceSetup sequenceSetup(const SceneFile& file) {
	Shot shot           = file.shot;
	shot.render.samples = stillFrames;
	shot.render.batch   = 2;
	return SequenceSetup {
		.shot         = shot,
		.lens         = lensOf(shot.camera, shot.render),
		.chosen       = defaultPrepared(),
		.preparations = defaultPreparations(),
	};
}

struct SequenceFrame {
	const SequenceScene& scene;
	const SequenceSetup& setup;
	const Lens& lens;
	ExecutionMember execution;
	const std::vector<ReconstructionMember>& reconstruction;
};

FrameInput sequenceFrameInput(const SequenceFrame& frame, Progress& progress) {
	return FrameInput {
		.geometry     = frame.scene.geometry,
		.looks        = frame.scene.file.looks,
		.lighting     = frame.scene.file.lighting,
		.shot         = frame.setup.shot,
		.lens         = frame.lens,
		.chosen       = frame.setup.chosen,
		.preparations = frame.setup.preparations,
		.features     = frame.scene.features,
		.stopping     = StoppingMember::FixedCount,
		.smoothGlass  = SmoothGlass::Exact,
		.execution    = frame.execution,
		.threads      = 4,
		.progress     = progress,
	};
}

Stepped sequenceStepped(const SequenceFrame& frame, const FrameStep& step, const History& history) {
	Progress progress      = freshProgress();
	const FrameInput input = sequenceFrameInput(frame, progress);

	const SequenceInput sequence = {
		.frame          = input,
		.step           = step,
		.reconstruction = frame.reconstruction,
		.surfaced       = false,
	};

	return steppedFrame(sequence, history);
}

struct StillSequence {
	Stepped last;
	FrameStep step;
};

StillSequence stillSequence(const SequenceFrame& frame) {
	const StepQuery query = {
		.lens        = frame.lens,
		.film        = stillFilm(),
		.change      = Change::None,
		.keepsGuides = false,
	};

	FrameStep step  = firstStep(query);
	Stepped stepped = sequenceStepped(frame, step, emptyHistory());
	for (int f = 1; f < stillFrames; ++f) {
		step    = nextStep(step, query);
		stepped = sequenceStepped(frame, step, stepped.history);
	}
	return StillSequence {
		.last = stepped,
		.step = step,
	};
}

bool sameImage(const Image& a, const Image& b) {
	return a.width == b.width && a.height == b.height && a.rgb.size() == b.rgb.size() && sameBytes(a.rgb.data(), b.rgb.data(), a.rgb.size() * sizeof(float));
}

bool sameFrameRecord(const FrameRecord& a, const FrameRecord& b) {
	if (a.width != b.width || a.height != b.height || a.parts.size() != b.parts.size() || a.surfaces.size() != b.surfaces.size()) {
		return false;
	}
	for (std::size_t k = 0; k < a.parts.size(); ++k) {
		if (!sameColour(a.parts[k].total, b.parts[k].total) || !sameFirstSurface(a.surfaces[k], b.surfaces[k])) {
			return false;
		}
	}
	return true;
}

bool sameGuideRow(const GuideRow& a, const GuideRow& b) {
	const bool sized = a.heads.size() == b.heads.size() && a.lumps.size() == b.lumps.size();
	return sized && sameBytes(a.heads.data(), b.heads.data(), a.heads.size() * sizeof(PackedGuide)) && sameBytes(a.lumps.data(), b.lumps.data(), a.lumps.size() * sizeof(PackedLump));
}

bool guidesAnything(const History& history) {
	for (const GuideRow& row : history.prepared.sampling.guides.rows) {
		if (!row.heads.empty()) {
			return true;
		}
	}
	return false;
}

bool sameGuides(const History& a, const History& b) {
	const HighlightGuides& x = a.prepared.sampling.guides;
	const HighlightGuides& y = b.prepared.sampling.guides;
	if (x.width != y.width || x.heads != y.heads || x.rows.size() != y.rows.size()) {
		return false;
	}
	for (std::size_t k = 0; k < x.rows.size(); ++k) {
		if (!sameGuideRow(x.rows[k], y.rows[k])) {
			return false;
		}
	}
	return true;
}

bool startedOver(const std::vector<Estimate>& estimates) {
	for (const Estimate& estimate : estimates) {
		if (estimate.taken != 1) {
			return false;
		}
	}
	return !estimates.empty();
}

Lens movedLens(const Lens& lens) {
	Lens moved = lens;
	moved.eye  = moved.eye + moved.right * 0.05;
	return moved;
}

Stepped movedStep(const SequenceFrame& frame, const StillSequence& still, bool keepsGuides) {
	const Lens moved = movedLens(frame.lens);

	const StepQuery query = {
		.lens        = moved,
		.film        = stillFilm(),
		.change      = Change::None,
		.keepsGuides = keepsGuides,
	};

	const SequenceFrame next = {
		.scene          = frame.scene,
		.setup          = frame.setup,
		.lens           = moved,
		.execution      = frame.execution,
		.reconstruction = frame.reconstruction,
	};

	return sequenceStepped(next, nextStep(still.step, query), still.last.history);
}

void checkStillSequence(const char* path, const SequenceScene& scene) {
	const SequenceSetup setup                     = sequenceSetup(scene.file);
	const std::vector<ReconstructionMember> means = defaultReconstruction();

	const SequenceFrame one = {
		.scene          = scene,
		.setup          = setup,
		.lens           = setup.lens,
		.execution      = ExecutionMember::OneAtATime,
		.reconstruction = means,
	};

	const SequenceFrame waves = {
		.scene          = scene,
		.setup          = setup,
		.lens           = setup.lens,
		.execution      = ExecutionMember::Waves,
		.reconstruction = means,
	};

	Progress progress           = freshProgress();
	const Rendered still        = renderFrame(sequenceFrameInput(waves, progress));
	const StillSequence byOne   = stillSequence(one);
	const StillSequence byWaves = stillSequence(waves);
	const bool same             = sameImage(byOne.last.image, still.image) && sameImage(byWaves.last.image, still.image);
	std::printf("sequence %s: %d still frames with Mean %s a still frame of %d samples\n", path, stillFrames, same ? "equal" : "differ from", stillFrames);
	check(same, "a sequence of still frames with Mean shows the still frame of as many samples, to the byte, with either Execution member");
	check(sameFrameRecord(byOne.last.history.record, byWaves.last.history.record), "a sequence's records are the same to the byte with either Execution member");

	const Stepped moved = movedStep(waves, byWaves, false);
	check(startedOver(moved.history.estimates) && !sameImage(moved.image, still.image), "a moved camera starts every pixel's estimate over");

	if (!guidesAnything(byWaves.last.history)) {
		return;
	}
	const Stepped guided = movedStep(waves, byWaves, true);
	check(!sameGuides(moved.history, byWaves.last.history), "a moved camera surveys its guides again");
	check(sameGuides(guided.history, byWaves.last.history) && startedOver(guided.history.estimates), "a moved camera that keeps its guides keeps the frame before's and starts every estimate over");
}

Lens halvedLens(const SequenceSetup& setup) {
	Render render = setup.shot.render;
	render.width /= 2;
	render.height /= 2;
	return lensOf(setup.shot.camera, render);
}

StepQuery filmQuery(const Lens& lens, FilmPlan film) {
	return StepQuery {
		.lens        = lens,
		.film        = film,
		.change      = Change::None,
		.keepsGuides = true,
	};
}

bool takenEach(const std::vector<Estimate>& estimates, int taken) {
	for (const Estimate& estimate : estimates) {
		if (estimate.taken != taken) {
			return false;
		}
	}
	return !estimates.empty();
}

void checkFactorSequence(const SequenceScene& scene) {
	const SequenceSetup setup                         = sequenceSetup(scene.file);
	const Lens half                                   = halvedLens(setup);
	const std::vector<ReconstructionMember> upsampled = { ReconstructionMember::Mean, ReconstructionMember::Upsample };
	const std::vector<ReconstructionMember> means     = defaultReconstruction();

	const SequenceFrame halved = {
		.scene          = scene,
		.setup          = setup,
		.lens           = half,
		.execution      = ExecutionMember::Waves,
		.reconstruction = upsampled,
	};

	const SequenceFrame whole = {
		.scene          = scene,
		.setup          = setup,
		.lens           = setup.lens,
		.execution      = ExecutionMember::Waves,
		.reconstruction = means,
	};

	const FrameStep first  = firstStep(filmQuery(half, bayerFilm(2, 0)));
	const Stepped one      = sequenceStepped(halved, first, emptyHistory());
	const FrameStep second = nextStep(first, filmQuery(half, bayerFilm(2, 1)));
	const Stepped two      = sequenceStepped(halved, second, one.history);
	const FrameStep third  = nextStep(second, filmQuery(setup.lens, bayerFilm(1, 2)));
	const Stepped three    = sequenceStepped(whole, third, two.history);
	const bool shownSize   = two.image.width == setup.lens.width && two.image.height == setup.lens.height;
	check(shownSize && two.history.record.width == half.width && takenEach(two.history.estimates, 2), "a frame at a factor of 2 draws at half the size, shows the full size, and its next phase continues every drawn estimate");
	check(three.image.width == setup.lens.width && startedOver(three.history.estimates), "a new factor at the same shown size starts every estimate over");
}

void checkSequenceScene(const char* path) {
	SceneFile file           = sceneOrExit(path);
	file.shot.render.width   = 48;
	file.shot.render.height  = 30;
	file.shot.render.samples = 4;
	const TestTree tree      = testTree(file, 8);
	const Features features  = featuresOf(featureQueryOf(file, 1));

	const Geometry geometry = {
		.scene = file.scene,
		.tree  = tree.tree,
	};

	const SequenceScene scene = {
		.file     = file,
		.geometry = geometry,
		.features = features,
	};

	checkRecords(path, scene);
	checkStillSequence(path, scene);
	checkFactorSequence(scene);
}

Lens flatLens(int width, int height) {
	return Lens {
		.eye      = { 0, 0, 0 },
		.forward  = { 0, 0, -1 },
		.right    = { 1, 0, 0 },
		.up       = { 0, 1, 0 },
		.reach    = 1,
		.aspect   = double(width) / double(height),
		.aperture = 0,
		.focus    = 1,
		.width    = width,
		.height   = height,
	};
}

struct ShownTest {
	Lens lens;
	FrameRecord record;
	std::vector<Estimate> estimates;
};

ShownTest shownTest(int width, int height, int factor) {
	FrameRecord record = emptyFrameRecord();
	record.width       = width / factor;
	record.height      = height / factor;
	return ShownTest {
		.lens      = flatLens(width, height),
		.record    = record,
		.estimates = {},
	};
}

ShownFrame shownTestFrame(const ShownTest& test, FilmPlan film) {
	return ShownFrame {
		.width     = test.lens.width,
		.height    = test.lens.height,
		.lens      = test.lens,
		.before    = test.lens,
		.film      = film,
		.moved     = false,
		.restarted = false,
		.still     = 0,
		.record    = test.record,
		.estimates = test.estimates,
		.surfaces  = noSurfaces(),
		.threads   = 1,
	};
}

Image rampImage(int width, int height) {
	Image image = blankImage(width, height);
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const std::size_t k = (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 3;
			image.rgb[k]        = float(x);
			image.rgb[k + 1]    = float(y);
			image.rgb[k + 2]    = 0.25f;
		}
	}
	return image;
}

std::vector<float> shownLine(const Image& image, int channel, bool across) {
	const int count = across ? image.width : image.height;
	std::vector<float> line;
	for (int k = 0; k < count; ++k) {
		const int x = across ? k : image.width / 2;
		const int y = across ? image.height / 2 : k;
		line.push_back(image.rgb[(std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 3 + std::size_t(channel)]);
	}
	return line;
}

bool centredRamp(const std::vector<float>& line, int factor) {
	const int drawn = int(line.size()) / factor;
	if (line.front() != 0 || line.back() != float(drawn - 1)) {
		return false;
	}
	for (int i = 1; i + 1 < drawn; ++i) {
		const std::size_t left = std::size_t(i * factor + factor / 2 - 1);
		const float centre     = (line[left] + line[left + 1]) / 2;
		const float step       = line[left + 1] - line[left];
		if (std::abs(centre - float(i)) > 1e-5f || std::abs(step - 1.0f / float(factor)) > 1e-5f) {
			return false;
		}
	}
	return true;
}

bool everyChannel(const Image& image, float value) {
	for (const float channel : image.rgb) {
		if (channel != value) {
			return false;
		}
	}
	return true;
}

bool blueEverywhere(const Image& image, float value) {
	for (std::size_t k = 2; k < image.rgb.size(); k += 3) {
		if (image.rgb[k] != value) {
			return false;
		}
	}
	return true;
}

bool upsampledRamp(int factor) {
	const ShownTest test = shownTest(6 * factor, 4 * factor, factor);
	const FilmPlan film  = bayerFilm(factor, 1);
	const Image shown    = upsampledImage(rampImage(6, 4), shownTestFrame(test, film));
	const bool sized     = shown.width == 6 * factor && shown.height == 4 * factor;
	const bool across    = centredRamp(shownLine(shown, 0, true), factor);
	const bool down      = centredRamp(shownLine(shown, 1, false), factor);
	return sized && across && down && blueEverywhere(shown, 0.25f);
}

void checkUpsample() {
	const ShownTest test = shownTest(24, 16, 4);

	const Image constant = {
		.width  = 6,
		.height = 4,
		.rgb    = std::vector<float>(6 * 4 * 3, 0.375f),
	};

	const Image stretched = upsampledImage(constant, shownTestFrame(test, bayerFilm(4, 5)));
	const ShownTest same  = shownTest(6, 4, 1);
	const Image ramp      = rampImage(6, 4);
	const Image kept      = upsampledImage(ramp, shownTestFrame(same, bayerFilm(1, 3)));

	check(stretched.width == 24 && stretched.height == 16 && everyChannel(stretched, 0.375f), "Upsample keeps a constant image constant, at the shown size");
	check(sameImage(kept, ramp), "Upsample at a factor of 1 is the identity");
	check(upsampledRamp(2) && upsampledRamp(4), "Upsample puts each drawn pixel's value at the centre of its block, blends linearly between centres and clamps at the edges");
}

bool coversOnce(const std::vector<int>& hits) {
	for (const int hit : hits) {
		if (hit != 1) {
			return false;
		}
	}
	return true;
}

void checkShownPixels() {
	const ShownTest test = shownTest(12, 8, 4);
	std::vector<int> hits(96, 0);
	for (std::uint32_t k = 0; k < 16; ++k) {
		const ShownFrame frame = shownTestFrame(test, bayerFilm(4, k));
		for (std::size_t drawn = 0; drawn < 6; ++drawn) {
			hits[shownPixelOf(frame, drawn)] += 1;
		}
	}

	const FilmPlan phase = {
		.factor = 4,
		.phase  = 6,
	};

	const bool pairs = bayerFilm(2, 0).phase == 0 && bayerFilm(2, 1).phase == 3 && bayerFilm(2, 2).phase == 1 && bayerFilm(2, 3).phase == 2 && bayerFilm(2, 4).phase == 0;
	int quadrants    = 0;
	for (std::uint32_t k = 0; k < 4; ++k) {
		const int phase = bayerFilm(4, k).phase;
		quadrants |= 1 << (phase / 8 * 2 + phase % 4 / 2);
	}
	const bool spread = quadrants == 15;
	check(shownPixelOf(shownTestFrame(test, phase), 4) == 66, "drawn pixel (1, 1) at a factor of 4 and phase 6 aims at shown pixel (6, 5)");
	check(coversOnce(hits), "sixteen frames of a factor of 4 in the Bayer order aim at every shown pixel once");
	check(pairs && spread && bayerFilm(1, 7).phase == 0, "the Bayer order runs 0, 3, 1, 2 at a factor of 2, and its first four frames at a factor of 4 fall in the four quadrants");
}

void checkFilmSteps() {
	const Lens half         = flatLens(8, 6);
	const Lens whole        = flatLens(16, 12);
	const Lens wider        = flatLens(10, 6);
	const Lens moved        = movedLens(half);
	const FrameStep first   = firstStep(filmQuery(half, bayerFilm(2, 0)));
	const FrameStep phased  = nextStep(first, filmQuery(half, bayerFilm(2, 1)));
	const FrameStep again   = nextStep(phased, filmQuery(half, bayerFilm(2, 2)));
	const FrameStep factor  = nextStep(again, filmQuery(whole, bayerFilm(1, 3)));
	const FrameStep resized = nextStep(again, filmQuery(wider, bayerFilm(2, 3)));
	const FrameStep flown   = nextStep(again, filmQuery(moved, bayerFilm(2, 3)));
	check(phased.change == Change::None && phased.still == 1 && again.still == 2, "a new phase alone changes nothing and the still frames grow");
	check(factor.change == Change::Moved && !factor.keepsGuides && flown.change == Change::Moved && flown.keepsGuides, "a new factor at the same shown size is a Moved change that keeps no guides, where a moved camera keeps them");
	check(changedBy(resized.change, Change::Cut), "a new shown size is a Cut");
}

void checkSequence() {
	checkUpsample();
	checkShownPixels();
	checkFilmSteps();
	checkSequenceScene("scenes/chess-board.sdf");
	checkSequenceScene("scenes/gallery.sdf");
}

}
