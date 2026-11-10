#include "frame/frame.h"
#include "frame/preparation.h"
#include "frame/run.h"
#include "frame/sequence.h"
#include "sdf/scene.h"
#include "spectral/colour.h"
#include "syntax/evaluator.h"
#include "transport/feature.h"
#include "tree/tree.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace kodanuki;

const char* joinName(Join join) {
	switch (join) {
		case Join::Leaf: return "leaf";
		case Join::Merge: return "merge";
		case Join::Cut: return "cut";
		case Join::Clip: return "clip";
	}

	return "?";
}

void dumpNode(const Scene& scene, std::uint32_t index, int depth) {
	const Node& node = scene.nodes[index];
	std::printf("%*s%s", depth * 2, "", joinName(node.join));
	if (node.join == Join::Leaf) {
		const Leaf& leaf           = scene.leaves[node.leaf];
		const Placement& placement = leaf.placement;
		std::printf(" %s at %g %g %g stretch %g material %u\n", scene.programs[leaf.program].name.c_str(), placement.origin[0], placement.origin[1], placement.origin[2], placement.stretch, leaf.material);
		return;
	}
	std::printf(" radius %g\n", node.radius);
	for (const std::uint32_t child : node.children) {
		dumpNode(scene, child, depth + 1);
	}
}

void dump(const SceneFile& file) {
	const Scene& scene = file.scene;
	std::printf("%zu programs, %zu leaves, %zu nodes, %zu objects, %zu materials, %zu lights\n", scene.programs.size(), scene.leaves.size(), scene.nodes.size(), scene.objects.size(), file.looks.materials.size(), file.lighting.lights.size());
	for (const Program& program : scene.programs) {
		std::printf("program %s: %zu steps, %u arguments\n", program.name.c_str(), program.steps.size(), program.arguments);
	}
	for (const std::uint32_t object : scene.objects) {
		dumpNode(scene, object, 0);
	}
}

void probe(const Scene& scene, const std::string& path) {
	std::ifstream in(path);
	if (!in) {
		std::fprintf(stderr, "cannot read %s\n", path.c_str());
		std::exit(1);
	}
	std::vector<double> registers;
	math::Vector p = { 0, 0, 0 };
	while (in >> p.x >> p.y >> p.z) {
		const Probe probe = {
			.p         = p,
			.registers = registers,
		};

		std::printf("%.9g\n", sceneDistance(scene, probe));
	}
}

void renderNormals(const SceneFile& file, const Choices& choices, const std::string& path) {
	const Features features   = featuresOf(featureQueryOf(file, choices.threads));
	const BuiltGeometry built = builtGeometry(file, choices, features);
	const Lens lens           = lensOf(file.shot.camera, file.shot.render);

	const Geometry geometry = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	writeUngradedPpm(normalsImage(geometry, lens, choices.threads), path);
}

struct Outputs {
	std::string ppm;
	std::string pfm;
	std::string track;
	int frames;
	std::vector<ReconstructionMember> reconstruction;
	int factor;
};

struct Drawn {
	Rendered rendered;
	double buildSeconds;
	double renderSeconds;
};

Drawn drawnStill(const SceneFile& file, const Choices& choices, Watch& watch) {
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const Features features                           = featuresOf(featureQueryOf(file, choices.threads));
	const BuiltGeometry built                         = builtGeometry(file, choices, features);
	const std::chrono::steady_clock::time_point grown = std::chrono::steady_clock::now();
	watch.stage                                       = Stage::Frame;

	const Geometry geometry = {
		.scene = file.scene,
		.tree  = built.tree,
	};

	const Prepared chosen                             = chosenPrepared(choices);
	const std::vector<PreparationMember> preparations = chosenPreparations(choices);
	const Lens lens                                   = lensOf(file.shot.camera, file.shot.render);

	const FrameInput input = {
		.geometry     = geometry,
		.looks        = file.looks,
		.lighting     = file.lighting,
		.shot         = file.shot,
		.lens         = lens,
		.chosen       = chosen,
		.preparations = preparations,
		.features     = features,
		.stopping     = choices.stopping,
		.smoothGlass  = choices.smoothGlass,
		.execution    = choices.execution,
		.threads      = choices.threads,
		.progress     = watch.progress,
	};

	Rendered rendered                                = renderFrame(input);
	const std::chrono::steady_clock::time_point done = std::chrono::steady_clock::now();
	return Drawn {
		.rendered      = std::move(rendered),
		.buildSeconds  = std::chrono::duration<double>(grown - start).count(),
		.renderSeconds = std::chrono::duration<double>(done - grown).count(),
	};
}

void render(const SceneFile& file, const Choices& choices, const Outputs& outputs) {
	Watch watch = freshWatch(0);
	std::optional<Drawn> drawn;
	const auto work = [&]() {
		drawn = drawnStill(file, choices, watch);
	};

	watched(work, watch);
	writePpm(drawn->rendered.image, file.shot.grade, outputs.ppm);
	if (!outputs.pfm.empty()) {
		writePfm(drawn->rendered.image, outputs.pfm);
	}
	std::fprintf(stderr, "%dx%d, %.1f samples a pixel, tree %.2f s, render %.2f s\n", file.shot.render.width, file.shot.render.height, drawn->rendered.samples, drawn->buildSeconds, drawn->renderSeconds);
}

struct TrackScene {
	const SceneFile& file;
	const Choices& choices;
	const Geometry& geometry;
	const Features& features;
	const Prepared& chosen;
	const std::vector<PreparationMember>& preparations;
	const std::vector<TrackCamera>& track;
	const Outputs& outputs;
	Watch& watch;
};

Stepped trackFrame(const TrackScene& scene, const FrameStep& step, History history) {
	const FrameInput input = {
		.geometry     = scene.geometry,
		.looks        = scene.file.looks,
		.lighting     = scene.file.lighting,
		.shot         = scene.file.shot,
		.lens         = step.lens,
		.chosen       = scene.chosen,
		.preparations = scene.preparations,
		.features     = scene.features,
		.stopping     = scene.choices.stopping,
		.smoothGlass  = scene.choices.smoothGlass,
		.execution    = scene.choices.execution,
		.threads      = scene.choices.threads,
		.progress     = scene.watch.progress,
	};

	const SequenceInput sequence = {
		.frame          = input,
		.step           = step,
		.reconstruction = scene.outputs.reconstruction,
		.surfaced       = readsSurfaces(scene.outputs.reconstruction),
	};

	return steppedFrame(sequence, std::move(history));
}

Lens trackLens(const TrackScene& scene, int frame) {
	Render render = scene.file.shot.render;
	render.width /= scene.outputs.factor;
	render.height /= scene.outputs.factor;
	return lensOf(trackedCamera(scene.file.shot.camera, scene.track, frame), render);
}

bool keepsTrackGuides(const Outputs& outputs) {
	return outputs.reconstruction != defaultReconstruction();
}

FrameStep firstTrackStep(const TrackScene& scene) {
	const Lens lens = trackLens(scene, 0);

	const StepQuery query = {
		.lens        = lens,
		.film        = bayerFilm(scene.outputs.factor, 0),
		.change      = Change::None,
		.keepsGuides = keepsTrackGuides(scene.outputs),
	};

	return firstStep(query);
}

FrameStep nextTrackStep(const TrackScene& scene, const FrameStep& last, int frame) {
	const Lens lens = trackLens(scene, frame);

	const StepQuery query = {
		.lens        = lens,
		.film        = bayerFilm(scene.outputs.factor, std::uint32_t(frame)),
		.change      = Change::None,
		.keepsGuides = keepsTrackGuides(scene.outputs),
	};

	return nextStep(last, query);
}

std::string framePath(const std::string& directory, int frame, const char* extension) {
	char name[32];
	std::snprintf(name, sizeof(name), "frame-%04d.%s", frame, extension);
	return (std::filesystem::path(directory) / name).string();
}

void writtenFrame(const TrackScene& scene, const Image& image, int frame) {
	writePpm(image, scene.file.shot.grade, framePath(scene.outputs.ppm, frame, "ppm"));
	writePfm(image, framePath(scene.outputs.ppm, frame, "pfm"));
}

struct NoiseSum {
	double ofMean;
	double perSample;
	double mature;
};

double perSampleNoise(const TemporalPixel& pixel) {
	const double luminance = luminanceOf(pixel.colour);
	const double squared   = luminance * luminance;
	const double length    = double(pixel.length);
	return std::max(pixel.moment - squared, 0.0) * length / ((length - 1) * (squared + 0.01));
}

NoiseSum noiseSumOf(const TemporalState& state) {
	NoiseSum sum = {
		.ofMean    = 0,
		.perSample = 0,
		.mature    = 0,
	};

	for (const TemporalPixel& pixel : state.pixels) {
		if (pixel.length < 2) {
			continue;
		}
		const double noise = perSampleNoise(pixel);
		sum.ofMean += noise / double(pixel.length);
		sum.perSample += noise;
		sum.mature += 1;
	}
	return sum;
}

std::string noiseNote(const TemporalState& state) {
	if (state.pixels.empty()) {
		return {};
	}
	const NoiseSum sum    = noiseSumOf(state);
	const double count    = double(state.pixels.size());
	const double young    = sum.mature > 0 ? sum.perSample / sum.mature : 1;
	const double estimate = (sum.ofMean + (count - sum.mature) * young) / count;
	char note[48];
	std::snprintf(note, sizeof(note), ", noise %.6f", estimate);
	return note;
}

void writtenTrack(const TrackScene& scene) {
	std::filesystem::create_directories(scene.outputs.ppm);
	History history = emptyHistory();
	FrameStep step  = firstTrackStep(scene);
	for (int f = 0; f < scene.outputs.frames; ++f) {
		const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
		Stepped stepped                                   = trackFrame(scene, step, std::move(history));
		writtenFrame(scene, stepped.image, f);
		history                   = std::move(stepped.history);
		step                      = nextTrackStep(scene, step, f + 1);
		const double frameSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		scene.watch.frame         = f + 1;
		const StepTimes& times    = stepped.times;
		const std::string noise   = noiseNote(history.reconstruction.temporal);
		std::fprintf(stderr, "%sframe %d, %.3f s: prepare %.3f, trace %.3f, surfaces %.3f, reconstruct %.3f, keep %.3f%s\n", scene.watch.drawn ? "\r\x1b[K" : "", f, frameSeconds, times.preparations, times.trace, times.surfaces, times.reconstruction, times.keeping, noise.c_str());
	}
}

struct TrackRun {
	const SceneFile& file;
	const Choices& choices;
	const std::vector<TrackCamera>& track;
	const Outputs& outputs;
};

void drawnTrack(const TrackRun& run, Watch& watch) {
	const Features features                           = featuresOf(featureQueryOf(run.file, run.choices.threads));
	const BuiltGeometry built                         = builtGeometry(run.file, run.choices, features);
	const Prepared chosen                             = chosenPrepared(run.choices);
	const std::vector<PreparationMember> preparations = chosenPreparations(run.choices);
	watch.stage                                       = Stage::Frame;

	const Geometry geometry = {
		.scene = run.file.scene,
		.tree  = built.tree,
	};

	const TrackScene scene = {
		.file         = run.file,
		.choices      = run.choices,
		.geometry     = geometry,
		.features     = features,
		.chosen       = chosen,
		.preparations = preparations,
		.track        = run.track,
		.outputs      = run.outputs,
		.watch        = watch,
	};

	writtenTrack(scene);
}

std::vector<ReconstructionMember> upscaledReconstruction() {
	return { ReconstructionMember::Temporal, ReconstructionMember::Denoise, ReconstructionMember::Upscale };
}

void refusedTrack(const char* message) {
	std::fprintf(stderr, "%s\n", message);
	std::exit(1);
}

void checkedTrack(const Render& render, const Outputs& outputs) {
	if (outputs.factor > 1 && outputs.reconstruction != upscaledReconstruction()) {
		refusedTrack("--factor: 2 to 4 need --reconstruction temporal+denoise+upscale");
	}
	if (render.width % outputs.factor != 0 || render.height % outputs.factor != 0) {
		refusedTrack("--factor: the size must be a multiple of the factor");
	}
}

void renderTrack(const SceneFile& file, const Choices& choices, const Outputs& outputs) {
	checkedTrack(file.shot.render, outputs);
	const std::vector<TrackCamera> track = trackOrExit(outputs.track);
	Watch watch                          = freshWatch(outputs.frames);

	const TrackRun run = {
		.file    = file,
		.choices = choices,
		.track   = track,
		.outputs = outputs,
	};

	const auto work = [&]() {
		drawnTrack(run, watch);
	};

	watched(work, watch);
}

std::vector<ReconstructionMember> trackReconstruction(const std::string& name) {
	if (name == "mean") {
		return defaultReconstruction();
	}
	if (name == "temporal") {
		return { ReconstructionMember::Temporal };
	}
	if (name == "temporal+denoise") {
		return { ReconstructionMember::Temporal, ReconstructionMember::Denoise };
	}
	if (name == "temporal+denoise+upscale") {
		return upscaledReconstruction();
	}
	refusedTrack("--reconstruction: mean, temporal, temporal+denoise or temporal+denoise+upscale");
	return {};
}

int trackFactor(const std::string& value) {
	const int factor = std::stoi(value);
	if (factor < 1 || factor > 4) {
		refusedTrack("--factor: 1, 2, 3 or 4");
	}
	return factor;
}

Outputs readOutput(const Outputs& outputs, const std::string& flag, const std::string& value) {
	Outputs read        = outputs;
	read.ppm            = flag == "--out" ? value : read.ppm;
	read.pfm            = flag == "--pfm" ? value : read.pfm;
	read.track          = flag == "--track" ? value : read.track;
	read.frames         = flag == "--frames" ? std::stoi(value) : read.frames;
	read.reconstruction = flag == "--reconstruction" ? trackReconstruction(value) : read.reconstruction;
	read.factor         = flag == "--factor" ? trackFactor(value) : read.factor;
	return read;
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fprintf(stderr, "usage: kodanuki scene.sdf [--size w h] [--seed n] [--tolerance t] [--samples n] [--batch n] [--split n] [--surfaces n] [--collisions n] [--threads n] [--use key=value ...] [--region lx ly lz hx hy hz] [--least e] [--depth n] [--out image.ppm] [--pfm image.pfm] [--frames n --track cameras.txt --out directory [--reconstruction mean|temporal|temporal+denoise|temporal+denoise+upscale] [--factor 1|2|3|4]] [--dump] [--probe points.txt] [--normals out.ppm]\n");
		return 1;
	}
	SceneFile file  = sceneOrExit(argv[1]);
	Choices choices = defaultChoices();

	Outputs outputs = {
		.ppm            = {},
		.pfm            = {},
		.track          = {},
		.frames         = 0,
		.reconstruction = defaultReconstruction(),
		.factor         = 1,
	};

	for (int i = 2; i + 1 < argc; ++i) {
		const std::string flag = argv[i];

		const CommandLine arguments = {
			.count  = argc,
			.values = argv,
		};

		if (readSetting(file.shot, argv, i) || readChoice(choices, arguments, i)) {
			continue;
		}
		outputs = readOutput(outputs, flag, argv[i + 1]);
	}
	if (outputs.frames > 0 && !outputs.track.empty() && !outputs.ppm.empty()) {
		renderTrack(file, choices, outputs);
		return 0;
	}
	if (!outputs.ppm.empty()) {
		render(file, choices, outputs);
	}
	for (int i = 2; i < argc; ++i) {
		const std::string flag = argv[i];
		if (flag == "--dump") {
			dump(file);
		}
		if (flag == "--probe" && i + 1 < argc) {
			probe(file.scene, argv[++i]);
		}
		if (flag == "--normals" && i + 1 < argc) {
			renderNormals(file, choices, argv[++i]);
		}
	}
	return 0;
}
