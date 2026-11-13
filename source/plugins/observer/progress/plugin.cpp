#include "plugins/observer/progress/plugin.h"

#include "core/render.h"
#include "core/step.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <system_error>
#include <thread>
#include <vector>

namespace kodanuki {

constexpr double rateWindow = 10;

struct ProgressState {
	bool drawn;
	int frames;
	std::atomic<Stage> stage;
	std::atomic<int> finished;
	std::atomic<int> total;
	std::atomic<int> frame;
	std::atomic<bool> aside;
	std::promise<void> closed;
	std::thread drawer;
};

ProgressState& progressState(void* state) {
	return *static_cast<ProgressState*>(state);
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

bool drawingBegun(const ProgressState& state) {
	return state.frames > 0 ? state.stage == Stage::Frame : state.total > 0;
}

int countOf(const ProgressState& state) {
	return state.frames > 0 ? state.frame.load() : state.finished.load();
}

int allOf(const ProgressState& state) {
	return state.frames > 0 ? state.frames : state.total.load();
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

Marks nextMarks(const Marks& marks, const ProgressState& state, Clock::time_point now) {
	const Stage stage = state.stage;
	const bool begun  = marks.drawing || drawingBegun(state);

	const Seen seen = {
		.time  = now,
		.count = countOf(state),
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

double leftOf(const ProgressState& state, const Marks& marks, Clock::time_point now) {
	if (marks.seen.empty()) {
		return -1;
	}
	const double rate = rateOf(marks, now);
	return rate > 0 ? double(allOf(state) - marks.seen.back().count) / rate : -1;
}

Snapshot snapshotOf(const ProgressState& state, const Marks& marks, Clock::time_point now) {
	return Snapshot {
		.stage    = marks.stage,
		.finished = state.finished,
		.total    = state.total,
		.frame    = state.frame,
		.frames   = state.frames,
		.staged   = secondsSince(marks.staged, now),
		.drawing  = marks.drawing ? secondsSince(*marks.drawing, now) : 0,
		.left     = leftOf(state, marks, now),
	};
}

void drawnUntilClosed(ProgressState& state, std::future<void> closed) {
	Marks marks = {
		.stage   = state.stage,
		.staged  = Clock::now(),
		.drawing = std::nullopt,
		.seen    = {},
	};

	while (closed.wait_for(std::chrono::milliseconds(250)) != std::future_status::ready) {
		const Clock::time_point now = Clock::now();
		marks                       = nextMarks(marks, state, now);
		std::fprintf(stderr, "\r%s\x1b[K", progressLine(snapshotOf(state, marks, now)).c_str());
	}
	std::fputs("\r\x1b[K", stderr);
}

bool stderrTerminal() {
	std::error_code error;
	const bool character = std::filesystem::is_character_file("/dev/stderr", error);
	const bool null      = std::filesystem::equivalent("/dev/stderr", "/dev/null", error);
	return character && !null;
}

int clipFramesDrawn(const Renderer& renderer) {
	const Clip& clip = renderer.file.shot.clip;
	return clip.duration > 0 && !renderer.options.time ? clipFrames(clip) : 0;
}

void progressOpened(Renderer& renderer) {
	const std::shared_ptr<ProgressState> state(new ProgressState {
		.drawn    = stderrTerminal(),
		.frames   = clipFramesDrawn(renderer),
		.stage    = Stage::Tree,
		.finished = 0,
		.total    = 0,
		.frame    = 0,
		.aside    = false,
		.closed   = {},
		.drawer   = {},
	});

	keptState(renderer, "observer/progress", state);
	if (state->drawn) {
		state->drawer = std::thread(drawnUntilClosed, std::ref(*state), state->closed.get_future());
	}
}

void progressObserved(void* opaque, const StepEvent& event) {
	ProgressState& state = progressState(opaque);
	if (event.step == LoopStep::BuildWorld) {
		state.stage = event.done ? Stage::Frame : Stage::Tree;
	}
	if (event.step == LoopStep::DrawFrame && !event.done) {
		state.total += int(event.items);
	}
	if (event.step == LoopStep::Preparations || event.step == LoopStep::Surfaces) {
		state.aside = !event.done;
	}
	if (event.step == LoopStep::PlaceTile && !state.aside) {
		state.finished += 1;
	}
	if (event.step == LoopStep::DrawFrame && event.done) {
		state.frame += 1;
	}
}

void progressClosed(Renderer& renderer) {
	ProgressState& state = progressState(stateOf(renderer, "observer/progress"));
	if (state.drawer.joinable()) {
		state.closed.set_value();
		state.drawer.join();
	}
}

Plugin observerProgressPlugin() {
	Hooks hooks         = noHooks();
	hooks.openRenderer  = progressOpened;
	hooks.observed      = progressObserved;
	hooks.closeRenderer = progressClosed;
	return Plugin {
		.name   = "observer/progress",
		.point  = Point::Observer,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
