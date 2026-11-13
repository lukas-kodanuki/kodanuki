#include "plugins/source/scene/plugin.h"

#include "core/render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace kodanuki {

struct ScenePlay {
	std::int64_t played;
	bool started;
};

ScenePlay& scenePlay(const Renderer& renderer) {
	return *static_cast<ScenePlay*>(stateOf(renderer, "source/scene"));
}

void sceneOpened(Renderer& renderer) {
	const ScenePlay play = {
		.played  = 0,
		.started = false,
	};

	keptState(renderer, "source/scene", std::make_shared<ScenePlay>(play));
}

const Plugin* clipPlayer(const Renderer& renderer) {
	for (const Plugin& sink : renderer.sinks) {
		if (sink.hooks.playedClip) {
			return &sink;
		}
	}
	return nullptr;
}

bool cutBetween(const Clip& clip, double from, double to) {
	return std::any_of(clip.cuts.begin(), clip.cuts.end(), [&](double cut) {
		return cut > from && cut <= to;
	});
}

void sceneShown(Renderer& renderer, double time, bool cut) {
	if (cut) {
		renderer.frame.change = joinedChange(renderer.frame.change, Change::Cut);
	}
	sceneAt(renderer, time);
	renderer.frame.camera = renderer.file.shot.camera;
}

bool stillNext(Renderer& renderer, bool interactive) {
	sceneShown(renderer, renderer.options.time.value_or(0), false);
	return interactive || renderer.frame.index == 0;
}

bool clipNext(Renderer& renderer) {
	const Clip& clip = renderer.file.shot.clip;
	const int index  = renderer.frame.index;
	if (index >= clipFrames(clip)) {
		return false;
	}
	const bool cut = index > 0 && cutBetween(clip, (index - 1) / clip.rate, index / clip.rate);
	sceneShown(renderer, index / clip.rate, cut);
	return true;
}

std::int64_t lapOf(std::int64_t played, std::int64_t count) {
	return played / count - (played % count < 0 ? 1 : 0);
}

bool playedCut(const ScenePlay& play, const Clip& clip, std::int64_t played) {
	const std::int64_t count  = clipFrames(clip);
	const std::int64_t frame  = played - lapOf(played, count) * count;
	const std::int64_t before = play.played - lapOf(play.played, count) * count;
	const bool wrapped        = lapOf(played, count) != lapOf(play.played, count);
	return play.started && (wrapped || cutBetween(clip, std::min(frame, before) / clip.rate, std::max(frame, before) / clip.rate));
}

bool playedNext(Renderer& renderer, const Plugin& player) {
	const Clip clip           = renderer.file.shot.clip;
	const std::int64_t count  = clipFrames(clip);
	const std::int64_t played = std::int64_t(std::floor(player.hooks.playedClip(renderer) * clip.rate + 1e-6));
	ScenePlay& play           = scenePlay(renderer);
	const bool cut            = playedCut(play, clip, played);
	play.played               = played;
	play.started              = true;
	sceneShown(renderer, double(played - lapOf(played, count) * count) / clip.rate, cut);
	return true;
}

bool sceneNext(Renderer& renderer) {
	const Plugin* player = clipPlayer(renderer);
	if (renderer.options.time || !(renderer.file.shot.clip.duration > 0)) {
		return stillNext(renderer, player != nullptr);
	}
	return player ? playedNext(renderer, *player) : clipNext(renderer);
}

Plugin sourceScenePlugin() {
	Hooks hooks        = noHooks();
	hooks.openRenderer = sceneOpened;
	hooks.nextFrame    = sceneNext;
	return Plugin {
		.name   = "source/scene",
		.point  = Point::Source,
		.effect = Effect::Exact,
		.hooks  = hooks,
		.reads  = Reads::Nothing,
		.after  = {},
	};
}

} // namespace kodanuki
