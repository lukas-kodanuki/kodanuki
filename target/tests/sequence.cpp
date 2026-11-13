#include "check.h"
#include "core/frame.h"
#include "core/history.h"
#include "core/pixels.h"
#include "core/plugin.h"
#include "core/shown.h"
#include "plugins/reconstruction/upsample/upsample.h"
#include "plugins/strategy/join/pass.h"
#include "plugins/transport/paths/camera.h"
#include "plugins/transport/paths/feature.h"
#include "plugins/transport/paths/paths.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace kodanuki {

struct SequenceScene {
	const SceneFile& file;
	const Geometry& geometry;
	const PathsWorld& paths;
};

bool sameBytes(const void* a, const void* b, std::size_t size) {
	return std::memcmp(a, b, size) == 0;
}

bool sameColour(const Colour& a, const Colour& b) {
	return sameBytes(&a, &b, sizeof(Colour));
}

constexpr int stillFrames = 6;

struct SequenceSetup {
	Shot shot;
	Lens lens;
	std::vector<Plugin> preparations;
};

SequenceSetup sequenceSetup(const SceneFile& file) {
	Shot shot           = file.shot;
	shot.render.samples = stillFrames;
	shot.render.batch   = 2;
	return SequenceSetup {
		.shot         = shot,
		.lens         = lensOf(shot.camera, shot.render),
		.preparations = testPreparations(defaultPreparations()),
	};
}

struct SequenceFrame {
	const SequenceScene& scene;
	const SequenceSetup& setup;
	const Lens& lens;
	std::string execution;
	const std::vector<Plugin>& reconstruction;
	int perFrame;
};

FrameInput sequenceFrameInput(const SequenceFrame& frame, const Observing& observing) {
	return FrameInput {
		.geometry       = frame.scene.geometry,
		.looks          = frame.scene.file.looks,
		.lighting       = frame.scene.file.lighting,
		.shot           = frame.setup.shot,
		.lens           = frame.lens,
		.transport      = testPlugin("transport/paths"),
		.transportWorld = &frame.scene.paths,
		.preparations   = frame.setup.preparations,
		.stopping       = testPlugin("stopping/fixed_count"),
		.perFrame       = frame.perFrame,
		.execution      = frame.execution,
		.threads        = 4,
		.observing      = observing,
	};
}

Framed sequenceFramed(const SequenceFrame& frame, const FrameStep& step, const History& history) {
	const Observing observing = noObserving();
	return framedBy(sequenceFrameInput(frame, observing), step, frame.reconstruction, history);
}

struct StillSequence {
	Framed last;
	FrameStep step;
};

StillSequence stillSequence(const SequenceFrame& frame) {
	const StepQuery query = {
		.lens        = frame.lens,
		.film        = stillFilm(),
		.change      = Change::None,
		.keepsGuides = false,
	};

	FrameStep step = firstStep(query);
	Framed stepped = sequenceFramed(frame, step, emptyHistory());
	for (int f = 1; f < stillFrames; ++f) {
		step    = nextStep(step, query);
		stepped = sequenceFramed(frame, step, stepped.history);
	}
	return StillSequence {
		.last = stepped,
		.step = step,
	};
}

bool sameImage(const Image& a, const Image& b) {
	return a.width == b.width && a.height == b.height && a.rgb.size() == b.rgb.size() && sameBytes(a.rgb.data(), b.rgb.data(), a.rgb.size() * sizeof(float));
}

bool sameGuideRow(const GuideRow& a, const GuideRow& b) {
	const bool sized = a.heads.size() == b.heads.size() && a.lumps.size() == b.lumps.size();
	return sized && sameBytes(a.heads.data(), b.heads.data(), a.heads.size() * sizeof(PackedGuide)) && sameBytes(a.lumps.data(), b.lumps.data(), a.lumps.size() * sizeof(PackedLump));
}

const HighlightGuides& guidesOf(History history) {
	return highlightGuidesState(history.states);
}

History detachedHistory(History history) {
	States kept                          = std::move(history.states);
	history.states                       = States { .held = {} };
	highlightGuidesState(history.states) = highlightGuidesState(kept);
	skyChartState(history.states)        = skyChartState(kept);
	glowCellsState(history.states)       = glowCellsState(kept);
	mirrorBallsState(history.states)     = mirrorBallsState(kept);
	carriedLightState(history.states)    = carriedLightState(kept);
	return history;
}

bool guidesAnything(const History& history) {
	for (const GuideRow& row : guidesOf(history).rows) {
		if (!row.heads.empty()) {
			return true;
		}
	}
	return false;
}

bool sameGuides(const History& a, const History& b) {
	const HighlightGuides& x = guidesOf(a);
	const HighlightGuides& y = guidesOf(b);
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

bool startedOver(const std::vector<PixelSum>& estimates) {
	for (const PixelSum& estimate : estimates) {
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

Framed movedStep(const SequenceFrame& frame, const StillSequence& still, bool keepsGuides) {
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
		.perFrame       = frame.perFrame,
	};

	return sequenceFramed(next, nextStep(still.step, query), detachedHistory(still.last.history));
}

double sequenceDistance(const Image& a, const Image& b) {
	double squares = 0;
	for (std::size_t k = 0; k < a.rgb.size(); ++k) {
		squares += double(a.rgb[k] - b.rgb[k]) * double(a.rgb[k] - b.rgb[k]);
	}
	return std::sqrt(squares / double(a.rgb.size()));
}

double sequenceMean(const Image& image) {
	double total = 0;
	for (const float value : image.rgb) {
		total += double(value);
	}
	return total / double(image.rgb.size());
}

bool sequenceWithinNoise(const Framed& a, const Framed& b, double noise) {
	const double pixels = double(a.image.rgb.size()) / 3.0;
	const bool noisy    = sequenceDistance(a.image, b.image) <= 1.25 * noise;
	const bool unbiased = std::abs(sequenceMean(a.image) - sequenceMean(b.image)) <= 4.0 * noise / std::sqrt(pixels);
	const bool effort   = std::abs(a.samples - b.samples) <= 0.1 * a.samples;
	return a.image.rgb.size() == b.image.rgb.size() && noisy && unbiased && effort;
}

Framed reseededStill(const SequenceFrame& frame) {
	SequenceSetup setup = frame.setup;
	setup.shot.render.seed += 1;

	const SequenceFrame other = {
		.scene          = frame.scene,
		.setup          = setup,
		.lens           = frame.lens,
		.execution      = frame.execution,
		.reconstruction = frame.reconstruction,
		.perFrame       = 0,
	};

	const Observing observing = noObserving();
	return stillFrame(sequenceFrameInput(other, observing));
}

bool sameSums(const std::vector<PixelSum>& a, const std::vector<PixelSum>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (std::size_t k = 0; k < a.size(); ++k) {
		const double apart = std::abs(a[k].total.red - b[k].total.red) + std::abs(a[k].total.green - b[k].total.green) + std::abs(a[k].total.blue - b[k].total.blue);
		if (a[k].taken != b[k].taken || apart > 1e-5 * (1 + std::abs(a[k].total.green))) {
			return false;
		}
	}
	return true;
}

int sameLeaves(const FrameRecord& a, const FrameRecord& b) {
	int same = 0;
	for (std::size_t k = 0; k < a.surfaces.size() && k < b.surfaces.size(); ++k) {
		same += a.surfaces[k].leaf == b.surfaces[k].leaf ? 1 : 0;
	}
	return same;
}

int metFirsts(const FrameRecord& record) {
	int met = 0;
	for (const Surface& surface : record.surfaces) {
		met += surface.leaf != noLeaf ? 1 : 0;
	}
	return met;
}

void checkFrameRecords(const SequenceFrame& one, const SequenceFrame& waves, const History& unread) {
	const std::vector<Plugin> temporal = testPlugins({ "reconstruction/temporal" });

	const SequenceFrame readOne = {
		.scene          = one.scene,
		.setup          = one.setup,
		.lens           = one.lens,
		.execution      = one.execution,
		.reconstruction = temporal,
		.perFrame       = 1,
	};

	const SequenceFrame readWaves = {
		.scene          = waves.scene,
		.setup          = waves.setup,
		.lens           = waves.lens,
		.execution      = waves.execution,
		.reconstruction = temporal,
		.perFrame       = 1,
	};

	const FrameRecord byOne   = stillSequence(readOne).last.history.record;
	const FrameRecord byWaves = stillSequence(readWaves).last.history.record;
	const std::size_t pixels  = std::size_t(byOne.width) * std::size_t(byOne.height);
	const bool filled         = byOne.parts.size() == pixels && byOne.surfaces.size() == pixels && byWaves.parts.size() == pixels && byWaves.surfaces.size() == pixels;
	check(unread.record.width == byOne.width && unread.record.parts.empty() && unread.record.surfaces.empty(), "a frame whose Reconstruction members read no record keeps none");
	check(filled && metFirsts(byOne) > 0, "a frame whose Reconstruction members read its record keeps each pixel's light and first surface");
	check(filled && sameLeaves(byOne, byWaves) * 100 >= int(pixels) * 98, "a frame's record names the same first surfaces with either Execution member");
	check(sameSums(stillSequence(readWaves).last.history.estimates, unread.estimates), "a frame that keeps its record leaves every pixel's samples as one that keeps none draws them");
}

void checkStillSequence(const char* path, const SequenceScene& scene) {
	const SequenceSetup setup       = sequenceSetup(scene.file);
	const std::vector<Plugin> means = testPlugins({ "reconstruction/mean" });

	const SequenceFrame one = {
		.scene          = scene,
		.setup          = setup,
		.lens           = setup.lens,
		.execution      = "execution/single",
		.reconstruction = means,
		.perFrame       = 1,
	};

	const SequenceFrame waves = {
		.scene          = scene,
		.setup          = setup,
		.lens           = setup.lens,
		.execution      = "execution/packets",
		.reconstruction = means,
		.perFrame       = 1,
	};

	const SequenceFrame whole = {
		.scene          = scene,
		.setup          = setup,
		.lens           = setup.lens,
		.execution      = "execution/packets",
		.reconstruction = means,
		.perFrame       = 0,
	};

	const Observing observing   = noObserving();
	const Framed still          = stillFrame(sequenceFrameInput(whole, observing));
	const Framed again          = reseededStill(whole);
	const StillSequence byOne   = stillSequence(one);
	const StillSequence byWaves = stillSequence(waves);
	const double noise          = sequenceDistance(still.image, again.image);
	const bool same             = sequenceWithinNoise(still, byOne.last, noise) && sequenceWithinNoise(still, byWaves.last, noise);
	std::printf("sequence %s: %d still frames of one sample with Mean are %s one frame of %d samples, %.4g and %.4g apart at a noise of %.4g between two seeds\n", path, stillFrames, same ? "within noise of" : "apart from", stillFrames, sequenceDistance(still.image, byOne.last.image), sequenceDistance(still.image, byWaves.last.image), noise);
	check(noise > 0 && same, "frames of one sample on a still camera with Mean show the one frame of as many samples within noise, with either Execution member");
	checkFrameRecords(one, waves, byWaves.last.history);

	const Framed moved = movedStep(waves, byWaves, false);
	check(startedOver(moved.history.estimates) && !sameImage(moved.image, still.image), "a moved camera starts every pixel's estimate over");

	if (!guidesAnything(byWaves.last.history)) {
		return;
	}
	const Framed guided = movedStep(waves, byWaves, true);
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

bool takenEach(const std::vector<PixelSum>& estimates, int taken) {
	for (const PixelSum& estimate : estimates) {
		if (estimate.taken != taken) {
			return false;
		}
	}
	return !estimates.empty();
}

void checkFactorSequence(const SequenceScene& scene) {
	const SequenceSetup setup           = sequenceSetup(scene.file);
	const Lens half                     = halvedLens(setup);
	const std::vector<Plugin> upsampled = testPlugins({ "reconstruction/mean", "reconstruction/upsample" });
	const std::vector<Plugin> means     = testPlugins({ "reconstruction/mean" });

	const SequenceFrame halved = {
		.scene          = scene,
		.setup          = setup,
		.lens           = half,
		.execution      = "execution/packets",
		.reconstruction = upsampled,
		.perFrame       = 1,
	};

	const SequenceFrame whole = {
		.scene          = scene,
		.setup          = setup,
		.lens           = setup.lens,
		.execution      = "execution/packets",
		.reconstruction = means,
		.perFrame       = 1,
	};

	const FrameStep first  = firstStep(filmQuery(half, bayerFilm(2, 0)));
	const Framed one       = sequenceFramed(halved, first, emptyHistory());
	const FrameStep second = nextStep(first, filmQuery(half, bayerFilm(2, 1)));
	const Framed two       = sequenceFramed(halved, second, one.history);
	const FrameStep third  = nextStep(second, filmQuery(setup.lens, bayerFilm(1, 2)));
	const Framed three     = sequenceFramed(whole, third, two.history);
	const bool shownSize   = two.image.width == setup.lens.width && two.image.height == setup.lens.height;
	check(shownSize && two.history.record.width == half.width && takenEach(two.history.estimates, 2), "a frame at a factor of 2 draws at half the size, shows the full size, and its next phase continues every drawn estimate");
	check(three.image.width == setup.lens.width && startedOver(three.history.estimates), "a new factor at the same shown size starts every estimate over");
}

void checkSequenceScene(const char* path) {
	SceneFile file             = sceneOrExit(path);
	file.shot.render.width     = 48;
	file.shot.render.height    = 30;
	file.shot.render.samples   = 4;
	const TestTree tree        = testTree(file, 8);
	const ChosenMembers chosen = defaultChosen();

	const PathsWorld paths = {
		.features    = featuresOf(featureQueryOf(file, 1)),
		.smoothGlass = SmoothGlass::Exact,
		.sampling    = chosen.sampling,
		.strategies  = chosen.strategies,
	};

	const Geometry geometry = treeGeometry(file.scene, tree.tree);

	const SequenceScene scene = {
		.file     = file,
		.geometry = geometry,
		.paths    = paths,
	};

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
	std::vector<PixelSum> estimates;
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
		.recounted = false,
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
