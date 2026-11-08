#pragma once

#include "frame/execution.h"
#include "frame/frame.h"
#include "frame/preparation.h"
#include "frame/stopping.h"
#include "material/material.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"
#include "transport/feature.h"
#include "transport/sampler.h"
#include "transport/strategy.h"
#include "tree/detail.h"
#include "tree/pass.h"
#include "tree/tree.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kodanuki {

/**
 * How a `--use` key takes its value.
 */
enum class UseForm : std::uint8_t {
	/**
	 * Exactly one of the members.
	 */
	One,
	/**
	 * Any of the members joined by +, in any order, or none.
	 */
	Joined,
	/**
	 * The first member, then any of the others joined by +.
	 */
	Led,
};

/**
 * What a run is busy with, for its progress line.
 */
enum class Stage : std::uint8_t {
	/**
	 * Building the tree.
	 */
	Tree,
	/**
	 * Drawing: the preparations until the first tile pass begins, then the
	 * tiles.
	 */
	Frame,
};

/**
 * The members and settings a run takes from the command line, which
 * every program that draws frames shares.
 */
struct Choices {
	/**
	 * The Detail members.
	 */
	std::vector<DetailMember> members;
	/**
	 * The Build pass members, in their order.
	 */
	std::vector<PassMember> passes;
	/**
	 * The Strategy members, meet first.
	 */
	std::vector<StrategyMember> strategies;
	/**
	 * How glass of roughness 0 answers.
	 */
	SmoothGlass smoothGlass;
	/**
	 * The Execution member.
	 */
	ExecutionMember execution;
	/**
	 * The Stopping member.
	 */
	StoppingMember stopping;
	/**
	 * The Sampler member that draws the numbers.
	 */
	SamplerMember sequence;
	/**
	 * The Sampler member that draws directions off a surface.
	 */
	SamplerMember direction;
	/**
	 * The Sampler member that picks a light.
	 */
	SamplerMember selection;
	/**
	 * The Sampler members that aim at light, lamps first.
	 */
	std::vector<SamplerMember> aims;
	/**
	 * Whether the tree derives exact faces.
	 */
	bool faces;
	/**
	 * Whether region holds a region given by hand.
	 */
	bool framed;
	/**
	 * The region the tree is built over, where framed says so.
	 */
	Region region;
	/**
	 * The least cell size, 0 for the scene's.
	 */
	double least;
	/**
	 * The deepest the tree may grow, -1 for the region's own cap.
	 */
	int depth;
	/**
	 * How many threads.
	 */
	int threads;
};

/**
 * The tree of a run and the detail it was built to.
 */
struct BuiltGeometry {
	/**
	 * How fine the tree's cells were asked to be.
	 */
	Detail detail;
	/**
	 * The tree.
	 */
	Tree tree;
};

/**
 * One camera of a camera track: where the eye stands and the point it
 * looks at, as a scene's camera holds them.
 */
struct TrackCamera {
	/**
	 * Where the eye stands.
	 */
	math::Vector eye;
	/**
	 * The point it looks at.
	 */
	math::Vector target;
};

/**
 * A program's command line, as main() receives it.
 */
struct CommandLine {
	/**
	 * How many words.
	 */
	int count;
	/**
	 * The words; the first is the program.
	 */
	char** values;
};

/**
 * One `--use` key: its name, the members it takes and what it chooses.
 */
struct UseKey {
	/**
	 * The name before the `=`.
	 */
	std::string name;
	/**
	 * How the key takes its value.
	 */
	UseForm form;
	/**
	 * The members it takes.
	 */
	std::vector<std::string> members;
	/**
	 * What the key chooses, in one line.
	 */
	std::string meaning;
};

/**
 * What a run has done so far, which the run writes from its own thread
 * and the progress line reads from the main thread.
 */
struct Watch {
	/**
	 * Whether a progress line is drawn: stderr is a terminal.
	 */
	bool drawn;
	/**
	 * What the run is busy with.
	 */
	std::atomic<Stage> stage;
	/**
	 * The frame's count of finished tiles.
	 */
	Progress progress;
	/**
	 * How many frames of a sequence are finished.
	 */
	std::atomic<int> frame;
	/**
	 * How many frames the sequence draws; 0 for a still frame.
	 */
	int frames;
};

/**
 * What the progress line shows: one look at a run's watch, and the
 * times the main thread saw each part begin.
 */
struct Snapshot {
	/**
	 * What the run is busy with.
	 */
	Stage stage;
	/**
	 * How many tiles are finished.
	 */
	int finished;
	/**
	 * How many tiles the passes begun so far draw.
	 */
	int total;
	/**
	 * How many frames of a sequence are finished.
	 */
	int frame;
	/**
	 * How many frames the sequence draws; 0 for a still frame.
	 */
	int frames;
	/**
	 * The seconds since the stage began.
	 */
	double staged;
	/**
	 * The seconds since the first tile pass began, or for a sequence
	 * since its first frame began; 0 before.
	 */
	double drawing;
	/**
	 * The seconds left, estimated from the tiles (or frames) finished in
	 * the last ten seconds, or from all of them where none finished
	 * since; negative while nothing is finished.
	 */
	double left;
};

/**
 * Load a scene file, or print its error and exit 1 (rule 19).
 *
 * @param path The file.
 */
SceneFile sceneOrExit(const std::string& path);

/**
 * Return the choices of a run that names none: the default members,
 * exact faces, the scene's region and 8 threads.
 */
Choices defaultChoices();

/**
 * Return the choices with one `--use name=value` applied.
 *
 * A value it does not know prints what the name takes and exits 1.
 *
 * @param choices The choices so far.
 * @param use The text after `--use`.
 */
Choices withUse(const Choices& choices, const std::string& use);

/**
 * Return every `--use` key with its members, which the parsing of
 * `--use` names when it refuses a value.
 */
std::vector<UseKey> useKeys();

/**
 * Build the tree a scene is drawn with, flying the pilot first where
 * the Detail members name it.
 *
 * The build frees far more than the tree keeps, spread over the
 * threads' heaps; with glibc that memory is then handed back to the
 * system (malloc_trim), so that the frame's buffers do not come on top
 * of it.
 *
 * @param file The scene.
 * @param choices The members and settings.
 * @param features The Feature members.
 */
BuiltGeometry builtGeometry(const SceneFile& file, const Choices& choices, const Features& features);

/**
 * Return the default prepared members with the chosen strategies and
 * samplers in place, and emission split toward mirrors where connect is
 * chosen.
 *
 * @param choices The members and settings.
 */
Prepared chosenPrepared(const Choices& choices);

/**
 * Return the preparations the chosen members read, in the default
 * order.
 *
 * @param choices The members and settings.
 */
std::vector<PreparationMember> chosenPreparations(const Choices& choices);

/**
 * Read a command-line setting that overrides the scene's render
 * settings: --size, --seed, --tolerance, --samples, --batch, --surfaces
 * or --collisions.
 *
 * Where the flag at i is one, its values are read into the shot and i
 * moves to the last of them, and true comes back; otherwise false and
 * nothing changes.
 *
 * @param shot The shot the setting overrides.
 * @param argv The command line's words.
 * @param i The index of the flag.
 */
bool readSetting(Shot& shot, char** argv, int& i);

/**
 * Read a command-line choice of members and settings: --use, --region,
 * --least, --depth or --threads.
 *
 * Where the flag at i is one and its values are there, they are read
 * into the choices and i moves to the last of them, and true comes
 * back; otherwise false and nothing changes.
 *
 * @param choices The choices the flag changes.
 * @param arguments The command line.
 * @param i The index of the flag.
 */
bool readChoice(Choices& choices, const CommandLine& arguments, int& i);

/**
 * Read a camera track, or print its error and exit 1 (rule 19).
 *
 * The file holds one camera a line, six numbers apart by spaces: the
 * eye, then the target (reference_language.md, "Camera tracks").
 *
 * @param path The file.
 */
std::vector<TrackCamera> trackOrExit(const std::string& path);

/**
 * Return the camera a track draws a frame through: the scene's camera
 * with the eye and the target of the frame's line, the last line's for
 * every frame past the track's end.
 *
 * @param camera The scene's camera.
 * @param track The track, at least one camera.
 * @param frame The frame's index.
 */
Camera trackedCamera(const Camera& camera, const std::vector<TrackCamera>& track, int frame);

/**
 * Return a watch of a run that has not begun, which draws a progress
 * line where stderr is a terminal.
 *
 * @param frames How many frames the run draws as a sequence; 0 for a
 * still frame.
 */
Watch freshWatch(int frames);

/**
 * Run the work on a thread of its own and, where the watch says so,
 * draw its progress line on stderr from this thread about four times a
 * second until it is done, then clear the line. Where it does not, the
 * work runs on this thread and nothing is drawn.
 *
 * @param work The run.
 * @param watch The watch the work writes.
 */
void watched(const std::function<void()>& work, Watch& watch);

/**
 * Return the progress line of one look at a run: the tree's or the
 * preparations' running time, or a bar of finished tiles (or frames of a
 * sequence) with the share done, the time so far and the time left.
 *
 * @param snapshot The look.
 */
std::string progressLine(const Snapshot& snapshot);

}
