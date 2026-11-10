#include "frame/run.h"

#include "extras/detail_correct_pilot/measure.h"
#include "tree/build.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace kodanuki {

constexpr double rateWindow = 10;

SceneFile sceneOrExit(const std::string& path) {
	LoadedScene loaded = loadScene(path);
	if (!loaded.error.empty()) {
		std::fprintf(stderr, "%s\n", loaded.error.c_str());
		std::exit(1);
	}
	return std::move(loaded.file);
}

Choices defaultChoices() {
	const Region none = {
		.lower = { 0, 0, 0 },
		.size  = { 0, 0, 0 },
	};

	return Choices {
		.members     = defaultDetailMembers(),
		.passes      = defaultPassMembers(),
		.strategies  = defaultPrepared().strategies.members,
		.smoothGlass = SmoothGlass::Exact,
		.execution   = ExecutionMember::Waves,
		.stopping    = StoppingMember::ConfidenceInterval,
		.sequence    = defaultSampling().sequence,
		.direction   = defaultSampling().direction,
		.selection   = defaultSampling().selection,
		.aims        = defaultSampling().aims,
		.faces       = true,
		.framed      = false,
		.region      = none,
		.least       = 0,
		.depth       = -1,
		.threads     = 8,
	};
}

struct PilotBuild {
	const SceneFile& file;
	const Choices& choices;
	const Region& region;
	const Features& features;
};

Tree coarseTree(const PilotBuild& build, const Detail& detail) {
	const std::vector<DetailMember> members = { DetailMember::ByDistance };
	const std::vector<PassMember> none      = {};

	const SceneDetailQuery query = {
		.scene   = build.file.scene,
		.shot    = build.file.shot,
		.region  = build.region,
		.members = members,
	};

	Detail coarse = sceneDetail(query);
	coarse.least  = detail.least;
	coarse.share  = pilotCoarseShare;

	const BuildInput input = {
		.scene    = build.file.scene,
		.detail   = coarse,
		.passes   = none,
		.region   = build.region,
		.depthCap = build.choices.depth >= 0 ? build.choices.depth : depthCapOf(build.region, detail.least),
		.workers  = build.choices.threads,
		.faces    = build.choices.faces,
	};

	return buildTree(input);
}

Pilot measuredPilot(const PilotBuild& build, const Detail& detail) {
	const Shot& shot          = build.file.shot;
	const Tree coarse         = coarseTree(build, detail);
	const Materials materials = featuredMaterials(build.features, materialsOf(build.file.looks, build.choices.smoothGlass), build.choices.smoothGlass);
	const Lights lights       = lightsOf(build.file.lighting);
	const Lens lens           = lensOf(shot.camera, shot.render);
	const Prepared prepared   = corePrepared();

	const Geometry geometry = {
		.scene = build.file.scene,
		.tree  = coarse,
	};

	const Tracing tracing = {
		.geometry   = geometry,
		.scene      = build.file.scene,
		.looks      = build.file.looks,
		.materials  = materials,
		.lights     = lights,
		.lens       = lens,
		.sampling   = prepared.sampling,
		.strategies = prepared.strategies,
		.features   = build.features,
		.limits     = transportLimits(shot.render),
		.seed       = std::uint64_t(shot.render.seed),
		.film       = stillFilm(),
		.recording  = false,
	};

	const PilotFlight flight = {
		.tracing = tracing,
		.view    = detail.view,
		.threads = build.choices.threads,
	};

	const std::vector<Need> needs = pilotNeeds(flight);

	const PilotInput input = {
		.region = build.region,
		.needs  = needs,
		.view   = detail.view,
	};

	return pilotOf(input);
}

bool pilotChosen(const Choices& choices) {
	return std::find(choices.members.begin(), choices.members.end(), DetailMember::Pilot) != choices.members.end();
}

BuiltGeometry builtGeometry(const SceneFile& file, const Choices& choices, const Features& features) {
	const Region region = choices.framed ? choices.region : sceneRegion(file.scene, file.shot, choices.threads);

	const SceneDetailQuery query = {
		.scene   = file.scene,
		.shot    = file.shot,
		.region  = region,
		.members = choices.members,
	};

	Detail detail = sceneDetail(query);
	detail.least  = choices.least > 0 ? choices.least : detail.least;

	const PilotBuild pilot = {
		.file     = file,
		.choices  = choices,
		.region   = region,
		.features = features,
	};

	detail.pilot = pilotChosen(choices) ? measuredPilot(pilot, detail) : detail.pilot;
	releasedFreeMemory();

	const BuildInput input = {
		.scene    = file.scene,
		.detail   = detail,
		.passes   = choices.passes,
		.region   = region,
		.depthCap = choices.depth >= 0 ? choices.depth : depthCapOf(region, detail.least),
		.workers  = choices.threads,
		.faces    = choices.faces,
	};

	Tree tree = buildTree(input);
	releasedFreeMemory();
	return BuiltGeometry {
		.detail = std::move(detail),
		.tree   = std::move(tree),
	};
}

bool strategyChosen(const Choices& choices, StrategyMember member) {
	return std::find(choices.strategies.begin(), choices.strategies.end(), member) != choices.strategies.end();
}

std::vector<EmissionShare> chosenEmission(const Choices& choices) {
	const std::vector<EmissionShare> alone = defaultSampling().emission;

	const EmissionShare cosine = {
		.member = SamplerMember::Cosine,
		.share  = 0.5,
	};

	const EmissionShare mirrors = {
		.member = SamplerMember::TowardMirrors,
		.share  = 0.5,
	};

	const std::vector<EmissionShare> mixed = { cosine, mirrors };
	return strategyChosen(choices, StrategyMember::Connect) ? mixed : alone;
}

Prepared chosenPrepared(const Choices& choices) {
	Prepared chosen           = defaultPrepared();
	chosen.strategies.members = choices.strategies;
	chosen.sampling.sequence  = choices.sequence;
	chosen.sampling.direction = choices.direction;
	chosen.sampling.selection = choices.selection;
	chosen.sampling.aims      = choices.aims;
	chosen.sampling.emission  = chosenEmission(choices);
	return chosen;
}

bool aimChosen(const Choices& choices, SamplerMember member) {
	return std::find(choices.aims.begin(), choices.aims.end(), member) != choices.aims.end();
}

bool preparationWanted(const Choices& choices, PreparationMember member) {
	switch (member) {
		case PreparationMember::SkyChart: return aimChosen(choices, SamplerMember::SkyMap);
		case PreparationMember::GlowGrid: return aimChosen(choices, SamplerMember::GlowingSurfaces);
		case PreparationMember::MirrorBalls: return true;
		case PreparationMember::GuideSurvey: return choices.direction == SamplerMember::HighlightGuide;
		case PreparationMember::LightPass: return strategyChosen(choices, StrategyMember::Join) || strategyChosen(choices, StrategyMember::Connect);
	}

	return true;
}

std::vector<PreparationMember> chosenPreparations(const Choices& choices) {
	std::vector<PreparationMember> preparations;
	for (const PreparationMember member : defaultPreparations()) {
		if (preparationWanted(choices, member)) {
			preparations.push_back(member);
		}
	}
	return preparations;
}

std::vector<UseKey> useKeys() {
	const UseKey faces = {
		.name    = "faces",
		.form    = UseForm::One,
		.members = { "on", "off" },
		.meaning = "whether the tree derives exact faces in its cells",
	};

	const UseKey detail = {
		.name    = "detail",
		.form    = UseForm::Joined,
		.members = { "by_distance", "focus_on_view", "pilot", "split_hidden_primitives", "split_crowded_leaves" },
		.meaning = "what says how fine the tree's cells must be",
	};

	const UseKey passes = {
		.name    = "passes",
		.form    = UseForm::Joined,
		.members = { "split_beside_finer", "cut_long_programs", "face_slabs", "shared_coefficients", "shared_subtrees" },
		.meaning = "the build passes that rewrite the tree",
	};

	const UseKey strategies = {
		.name    = "strategies",
		.form    = UseForm::Led,
		.members = { "meet", "aim", "join", "connect" },
		.meaning = "how paths are made",
	};

	const UseKey smoothGlass = {
		.name    = "smooth_glass",
		.form    = UseForm::One,
		.members = { "exact", "lobe" },
		.meaning = "glass of roughness 0: an exact mirror and passage, or the narrowest lobe",
	};

	const UseKey execution = {
		.name    = "execution",
		.form    = UseForm::One,
		.members = { "one_at_a_time", "waves" },
		.meaning = "the order paths run in; the frame is the same to the byte",
	};

	const UseKey stopping = {
		.name    = "stopping",
		.form    = UseForm::One,
		.members = { "fixed", "confidence" },
		.meaning = "when a pixel is done: after --samples, or once its noise is within --tolerance",
	};

	const UseKey sequence = {
		.name    = "sequence",
		.form    = UseForm::One,
		.members = { "sobol", "independent" },
		.meaning = "the numbers a sample draws",
	};

	const UseKey direction = {
		.name    = "direction",
		.form    = UseForm::One,
		.members = { "material", "material+highlight_guide" },
		.meaning = "how a direction off a surface is drawn",
	};

	const UseKey selection = {
		.name    = "selection",
		.form    = UseForm::One,
		.members = { "each_lamp_first", "one_by_contribution" },
		.meaning = "how the light a shadow ray aims at is picked",
	};

	const UseKey aims = {
		.name    = "aims",
		.form    = UseForm::Led,
		.members = { "lamps", "sky_map", "glowing_surfaces" },
		.meaning = "what paths aim at for light",
	};

	return { faces, detail, passes, strategies, smoothGlass, execution, stopping, sequence, direction, selection, aims };
}

UseKey useKeyNamed(const std::string& name) {
	const std::vector<UseKey> keys = useKeys();
	return *std::find_if(keys.begin(), keys.end(), [&](const UseKey& key) {
		return key.name == name;
	});
}

std::string joinedNames(const std::vector<std::string>& names, const std::string& between) {
	std::string joined;
	for (const std::string& name : names) {
		joined += joined.empty() ? name : between + name;
	}
	return joined;
}

std::string ledText(const std::vector<std::string>& members) {
	std::string text = members.front();
	for (std::size_t k = 1; k < members.size(); ++k) {
		text += "[+" + members[k] + "]";
	}
	return text;
}

std::string membersText(const UseKey& key) {
	switch (key.form) {
		case UseForm::One: return joinedNames(key.members, " | ");
		case UseForm::Joined: return "none | any of " + joinedNames(key.members, "+");
		case UseForm::Led: return ledText(key.members);
	}

	return joinedNames(key.members, " | ");
}

std::string takesText(const std::string& name) {
	const UseKey key = useKeyNamed(name);
	return "--use " + name + "= takes " + membersText(key) + " (" + key.meaning + ")";
}

[[noreturn]] void refuse(const std::string& what) {
	std::fprintf(stderr, "kodanuki: %s\n", what.c_str());
	std::exit(1);
}

std::vector<std::string> namesIn(const std::string& value) {
	std::vector<std::string> names;
	std::size_t start = 0;
	while (value != "none" && start <= value.size()) {
		const std::size_t plus = std::min(value.find('+', start), value.size());
		names.push_back(value.substr(start, plus - start));
		start = plus + 1;
	}
	return names;
}

std::vector<DetailMember> detailMembers(const std::string& value) {
	std::vector<DetailMember> members;
	for (const std::string& name : namesIn(value)) {
		const std::optional<DetailMember> member = detailMemberNamed(name);
		if (!member) {
			refuse(takesText("detail") + ", not " + value);
		}
		members.push_back(*member);
	}
	return members;
}

std::vector<PassMember> passMembers(const std::string& value) {
	std::vector<PassMember> members;
	for (const std::string& name : namesIn(value)) {
		const std::optional<PassMember> member = passMemberNamed(name);
		if (!member) {
			refuse(takesText("passes") + ", not " + value);
		}
		members.push_back(*member);
	}
	std::sort(members.begin(), members.end());
	return members;
}

std::optional<StrategyMember> strategyNamed(const std::string& name) {
	if (name == "aim") {
		return StrategyMember::Aim;
	}
	if (name == "join") {
		return StrategyMember::Join;
	}
	if (name == "connect") {
		return StrategyMember::Connect;
	}
	return std::nullopt;
}

std::vector<StrategyMember> strategyMembers(const std::string& value) {
	std::vector<StrategyMember> members  = { StrategyMember::Meet };
	const std::vector<std::string> names = namesIn(value);
	if (names.empty() || names.front() != "meet") {
		refuse(takesText("strategies") + ", not " + value);
	}
	for (std::size_t k = 1; k < names.size(); ++k) {
		const std::optional<StrategyMember> member = strategyNamed(names[k]);
		if (!member) {
			refuse(takesText("strategies") + ", not " + value);
		}
		members.push_back(*member);
	}
	return members;
}

std::vector<SamplerMember> aimMembers(const std::string& value) {
	std::vector<SamplerMember> members = { SamplerMember::Lamps };
	for (const std::string& name : namesIn(value)) {
		if (name != "lamps" && name != "sky_map" && name != "glowing_surfaces") {
			refuse(takesText("aims") + ", not " + value);
		}
		if (name != "lamps") {
			members.push_back(name == "sky_map" ? SamplerMember::SkyMap : SamplerMember::GlowingSurfaces);
		}
	}
	return members;
}

bool readSamplingUse(Choices& choices, const std::string& name, const std::string& value) {
	if (name == "sequence" && (value == "sobol" || value == "independent")) {
		choices.sequence = value == "sobol" ? SamplerMember::Sobol : SamplerMember::Independent;
		return true;
	}
	if (name == "direction" && (value == "material" || value == "material+highlight_guide")) {
		choices.direction = value == "material" ? SamplerMember::Material : SamplerMember::HighlightGuide;
		return true;
	}
	if (name == "selection" && (value == "each_lamp_first" || value == "one_by_contribution")) {
		choices.selection = value == "each_lamp_first" ? SamplerMember::EachLampFirst : SamplerMember::ByContribution;
		return true;
	}
	if (name == "aims") {
		choices.aims = aimMembers(value);
		return true;
	}
	return false;
}

std::string useKeysText() {
	std::vector<std::string> keys;
	for (const UseKey& key : useKeys()) {
		keys.push_back(key.name + "=" + membersText(key));
	}
	return "--use takes " + joinedNames(keys, ", ");
}

void readUse(Choices& choices, const std::string& use) {
	const std::size_t equals = use.find('=');
	const std::string name   = use.substr(0, equals);
	const std::string value  = equals == std::string::npos ? "" : use.substr(equals + 1);
	if (name == "faces" && (value == "on" || value == "off")) {
		choices.faces = value == "on";
		return;
	}
	if (name == "detail") {
		choices.members = detailMembers(value);
		return;
	}
	if (name == "passes") {
		choices.passes = passMembers(value);
		return;
	}
	if (name == "strategies") {
		choices.strategies = strategyMembers(value);
		return;
	}
	if (name == "smooth_glass" && (value == "exact" || value == "lobe")) {
		choices.smoothGlass = value == "exact" ? SmoothGlass::Exact : SmoothGlass::Lobe;
		return;
	}
	if (name == "execution" && (value == "one_at_a_time" || value == "waves")) {
		choices.execution = value == "waves" ? ExecutionMember::Waves : ExecutionMember::OneAtATime;
		return;
	}
	if (name == "stopping" && (value == "fixed" || value == "confidence")) {
		choices.stopping = value == "fixed" ? StoppingMember::FixedCount : StoppingMember::ConfidenceInterval;
		return;
	}
	if (readSamplingUse(choices, name, value)) {
		return;
	}
	refuse(useKeysText() + "; not " + use);
}

Choices withUse(const Choices& choices, const std::string& use) {
	Choices chosen = choices;
	readUse(chosen, use);
	return chosen;
}

void refuseTrack(const std::string& path, const std::string& message) {
	std::fprintf(stderr, "%s: %s\n", path.c_str(), message.c_str());
	std::exit(1);
}

std::optional<TrackCamera> trackLineCamera(const std::string& line) {
	std::istringstream in(line);
	double values[6];
	for (double& value : values) {
		if (!(in >> value)) {
			return std::nullopt;
		}
	}
	std::string rest;
	if (in >> rest) {
		return std::nullopt;
	}
	return TrackCamera {
		.eye    = { float(values[0]), float(values[1]), float(values[2]) },
		.target = { float(values[3]), float(values[4]), float(values[5]) },
	};
}

std::vector<TrackCamera> trackOrExit(const std::string& path) {
	std::ifstream in(path);
	if (!in) {
		refuseTrack(path, "cannot read the track");
	}
	std::vector<TrackCamera> track;
	std::string line;
	for (int number = 1; std::getline(in, line); ++number) {
		const std::optional<TrackCamera> camera = trackLineCamera(line);
		if (!camera) {
			refuseTrack(path + ":" + std::to_string(number), "a track line holds six numbers: the eye, then the target");
		}
		track.push_back(*camera);
	}
	if (track.empty()) {
		refuseTrack(path, "a track holds at least one camera");
	}
	return track;
}

Camera trackedCamera(const Camera& camera, const std::vector<TrackCamera>& track, int frame) {
	const TrackCamera& tracked = track[std::size_t(std::min(frame, int(track.size()) - 1))];
	Camera moved               = camera;
	moved.eye                  = tracked.eye;
	moved.target               = tracked.target;
	return moved;
}

bool stderrTerminal() {
	std::error_code error;
	const bool character = std::filesystem::is_character_file("/dev/stderr", error);
	const bool null      = std::filesystem::equivalent("/dev/stderr", "/dev/null", error);
	return character && !null;
}

Watch freshWatch(int frames) {
	return Watch {
		.drawn    = stderrTerminal(),
		.stage    = Stage::Tree,
		.progress = freshProgress(),
		.frame    = 0,
		.frames   = frames,
	};
}

std::string durationText(double seconds) {
	char text[32];
	const int whole = int(seconds);
	if (seconds < 60) {
		std::snprintf(text, sizeof(text), "%.1f s", seconds);
		return text;
	}
	if (seconds < 3600) {
		std::snprintf(text, sizeof(text), "%d min %02d s", whole / 60, whole % 60);
		return text;
	}
	std::snprintf(text, sizeof(text), "%d h %02d min", whole / 3600, whole / 60 % 60);
	return text;
}

std::string barText(double share) {
	const int width = 24;
	const int full  = std::clamp(int(share * width), 0, width);
	return "[" + std::string(std::size_t(full), '#') + std::string(std::size_t(width - full), '.') + "]";
}

std::string leftText(double left) {
	return left >= 0 ? ", about " + durationText(left) + " left" : "";
}

std::string shareLine(const std::string& counted, double share, const Snapshot& snapshot) {
	char percent[16];
	std::snprintf(percent, sizeof(percent), " %3d%% ", int(share * 100));
	return barText(share) + " " + counted + percent + durationText(snapshot.drawing) + leftText(snapshot.left);
}

std::string framesLine(const Snapshot& snapshot) {
	const double share = double(snapshot.frame) / double(snapshot.frames);
	return "frames " + shareLine(std::to_string(snapshot.frame) + "/" + std::to_string(snapshot.frames), share, snapshot);
}

std::string tilesLine(const Snapshot& snapshot) {
	const double share = double(snapshot.finished) / double(snapshot.total);
	return "render " + shareLine(std::to_string(snapshot.finished) + "/" + std::to_string(snapshot.total) + " tiles", share, snapshot);
}

std::string progressLine(const Snapshot& snapshot) {
	if (snapshot.stage == Stage::Tree) {
		return "tree " + durationText(snapshot.staged);
	}
	if (snapshot.frames > 0) {
		return framesLine(snapshot);
	}
	if (snapshot.total == 0) {
		return "preparations " + durationText(snapshot.staged);
	}
	return tilesLine(snapshot);
}

using Clock = std::chrono::steady_clock;

struct Seen {
	Clock::time_point time;
	int count;
};

struct Marks {
	Stage stage;
	Clock::time_point staged;
	std::optional<Clock::time_point> drawing;
	std::vector<Seen> seen;
};

bool drawingBegun(const Watch& watch) {
	return watch.frames > 0 ? watch.stage == Stage::Frame : watch.progress.total > 0;
}

int countOf(const Watch& watch) {
	return watch.frames > 0 ? watch.frame.load() : watch.progress.finished.load();
}

int allOf(const Watch& watch) {
	return watch.frames > 0 ? watch.frames : watch.progress.total.load();
}

double secondsSince(Clock::time_point start, Clock::time_point now) {
	return std::chrono::duration<double>(now - start).count();
}

std::vector<Seen> recentSeen(const std::vector<Seen>& seen, const Seen& now) {
	std::vector<Seen> recent;
	for (const Seen& earlier : seen) {
		if (secondsSince(earlier.time, now.time) <= rateWindow) {
			recent.push_back(earlier);
		}
	}
	recent.push_back(now);
	return recent;
}

Marks nextMarks(const Marks& marks, const Watch& watch, Clock::time_point now) {
	const Stage stage = watch.stage;
	const bool begun  = marks.drawing || drawingBegun(watch);

	const Seen seen = {
		.time  = now,
		.count = countOf(watch),
	};

	return Marks {
		.stage   = stage,
		.staged  = stage == marks.stage ? marks.staged : now,
		.drawing = marks.drawing || !begun ? marks.drawing : now,
		.seen    = begun ? recentSeen(marks.seen, seen) : marks.seen,
	};
}

double rateOf(const Marks& marks, Clock::time_point now) {
	const Seen& first = marks.seen.front();
	const Seen& last  = marks.seen.back();
	if (last.count > first.count) {
		return double(last.count - first.count) / secondsSince(first.time, last.time);
	}
	const double drawn = marks.drawing ? secondsSince(*marks.drawing, now) : 0;
	return last.count > 0 && drawn > 0 ? last.count / drawn : 0;
}

double leftOf(const Watch& watch, const Marks& marks, Clock::time_point now) {
	if (marks.seen.empty()) {
		return -1;
	}
	const double rate = rateOf(marks, now);
	return rate > 0 ? double(allOf(watch) - marks.seen.back().count) / rate : -1;
}

Snapshot snapshotOf(const Watch& watch, const Marks& marks, Clock::time_point now) {
	return Snapshot {
		.stage    = marks.stage,
		.finished = watch.progress.finished,
		.total    = watch.progress.total,
		.frame    = watch.frame,
		.frames   = watch.frames,
		.staged   = secondsSince(marks.staged, now),
		.drawing  = marks.drawing ? secondsSince(*marks.drawing, now) : 0,
		.left     = leftOf(watch, marks, now),
	};
}

void drawnUntil(const std::future<void>& done, const Watch& watch) {
	const Clock::time_point start = Clock::now();

	Marks marks = {
		.stage   = watch.stage,
		.staged  = start,
		.drawing = std::nullopt,
		.seen    = {},
	};

	while (done.wait_for(std::chrono::milliseconds(250)) != std::future_status::ready) {
		const Clock::time_point now = Clock::now();
		marks                       = nextMarks(marks, watch, now);
		std::fprintf(stderr, "\r%s\x1b[K", progressLine(snapshotOf(watch, marks, now)).c_str());
	}
}

void watched(const std::function<void()>& work, Watch& watch) {
	if (!watch.drawn) {
		work();
		return;
	}
	std::future<void> done = std::async(std::launch::async, work);
	drawnUntil(done, watch);
	std::fputs("\r\x1b[K", stderr);
	done.get();
}

int splitArgument(const std::string& value) {
	const int split = std::stoi(value);
	if (split != 0 && split != 1 && split != 2 && split != 4 && split != 8) {
		std::fprintf(stderr, "--split: 0, 1, 2, 4 or 8\n");
		std::exit(1);
	}
	return split;
}

bool readSetting(Shot& shot, char** argv, int& i) {
	const std::string flag = argv[i];
	Render& render         = shot.render;
	if (flag == "--size") {
		render.width  = std::stoi(argv[++i]);
		render.height = std::stoi(argv[++i]);
		return true;
	}
	if (flag == "--seed") {
		render.seed = std::stoi(argv[++i]);
		return true;
	}
	if (flag == "--tolerance") {
		render.tolerance = std::stof(argv[++i]);
		return true;
	}
	if (flag == "--samples") {
		render.samples = std::stoi(argv[++i]);
		return true;
	}
	if (flag == "--batch") {
		render.batch = std::max(0, std::stoi(argv[++i]));
		return true;
	}
	if (flag == "--split") {
		render.split = splitArgument(argv[++i]);
		return true;
	}
	if (flag == "--surfaces") {
		render.surfaces = std::max(1, std::stoi(argv[++i]));
		return true;
	}
	if (flag == "--collisions") {
		render.collisions = std::max(1, std::stoi(argv[++i]));
		return true;
	}
	return false;
}

Region regionArgument(char** argv, int at) {
	Region region = {
		.lower = { 0, 0, 0 },
		.size  = { 0, 0, 0 },
	};

	for (int axis = 0; axis < 3; ++axis) {
		region.lower[axis] = std::stod(argv[at + axis]);
		region.size[axis]  = std::stod(argv[at + 3 + axis]) - region.lower[axis];
	}
	return region;
}

bool readChoice(Choices& choices, const CommandLine& arguments, int& i) {
	const int argc         = arguments.count;
	char** argv            = arguments.values;
	const std::string flag = argv[i];
	if (flag == "--use" && i + 1 < argc) {
		choices = withUse(choices, argv[++i]);
		return true;
	}
	if (flag == "--region" && i + 6 < argc) {
		choices.region = regionArgument(argv, i + 1);
		choices.framed = true;
		i += 6;
		return true;
	}
	if (flag == "--least" && i + 1 < argc) {
		choices.least = std::stod(argv[++i]);
		return true;
	}
	if (flag == "--depth" && i + 1 < argc) {
		choices.depth = std::stoi(argv[++i]);
		return true;
	}
	if (flag == "--threads" && i + 1 < argc) {
		choices.threads = std::max(1, std::stoi(argv[++i]));
		return true;
	}
	return false;
}

}
