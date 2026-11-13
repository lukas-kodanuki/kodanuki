#pragma once

#include "core/geometry.h"
#include "core/pixels.h"
#include "core/stopping.h"
#include "sdf/scene.h"
#include "syntax/evaluator.h"

#include <cstdint>
#include <optional>
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
	 * Any of the members joined by +, or none; they run in the key's
	 * order.
	 */
	Joined,
	/**
	 * Any of the members joined by +, or none; they run in the order
	 * given.
	 */
	Listed,
};

/**
 * The members and settings a run takes from the command line, which
 * every program that draws frames shares.
 */
struct Choices {
	/**
	 * The Geometry plugin's name, as `geometry/tree`.
	 */
	std::string geometry;
	/**
	 * The Transport plugin's name, as `transport/paths`.
	 */
	std::string transport;
	/**
	 * The Execution plugin's name, as `execution/packets`.
	 */
	std::string execution;
	/**
	 * The Backend plugin's name, as `backend/cpu`.
	 */
	std::string backend;
	/**
	 * The Stopping plugin's name, as `stopping/confidence_interval`.
	 */
	std::string stopping;
	/**
	 * The members of the points `geometry/tree` and `transport/paths`
	 * own (Detail, Build, Strategy, Sampler) that are on, by plugin
	 * name, as `sampler/sobol`, in their keys' order.
	 */
	std::vector<std::string> owned;
	/**
	 * How glass of roughness 0 answers, by its `--use smooth_glass=`
	 * name.
	 */
	std::string smoothGlass;
	/**
	 * Whether `geometry/tree` derives exact faces.
	 */
	bool faces;
	/**
	 * Whether region holds a region given by hand.
	 */
	bool framed;
	/**
	 * The region the world is built over, where framed says so.
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
	 * How many threads: the workers `backend/cpu` opens, and the
	 * threads of the plugins' own work.
	 */
	int threads;
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
 * A key named after a point takes the names of its plugins there.
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
 * Load a scene file at a time, or print its error and exit 1 (rule 19).
 *
 * @param path The file.
 * @param time The number its `time` is bound to.
 */
SceneFile sceneAtOrExit(const std::string& path, double time);

/**
 * Load a scene file as a still, its `time` 0, or print its error and
 * exit 1 (rule 19).
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
 * Return the name of the backend that hosts every chosen backend's
 * workers on its threads and runs a step another backend holds no
 * kernel for: `backend/cpu`.
 */
std::string fallbackBackend();

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
 * What a run draws with, read from the command line: the Source and
 * Sink plugins, the members and settings, and where the frames go.
 */
struct Options {
	/**
	 * The Source plugins, in the order they run.
	 */
	std::vector<std::string> sources;
	/**
	 * The Sink plugins, in the order they run.
	 */
	std::vector<std::string> sinks;
	/**
	 * The Observer plugins, in the order they are told of a step.
	 */
	std::vector<std::string> observers;
	/**
	 * The members and settings.
	 */
	Choices choices;
	/**
	 * --out: the image of a still, or the directory of a clip's frames.
	 */
	std::string out;
	/**
	 * --pfm: the linear image of a still, empty for none.
	 */
	std::string pfm;
	/**
	 * --time: the time a still of the file is drawn at, of a file with a
	 * clip or without; none where the file's clip plays.
	 */
	std::optional<double> time;
	/**
	 * The Reconstruction plugins' names, in order.
	 */
	std::vector<std::string> reconstruction;
	/**
	 * How many samples every pixel draws in a frame, whatever the
	 * Stopping member; zero where the member decides.
	 */
	int perFrame;
	/**
	 * The shown size over the drawn size, the same on both sides: 1 to 4.
	 */
	int factor;
	/**
	 * The width frames are drawn at; the shown width is factor times it.
	 */
	int width;
	/**
	 * The height frames are drawn at.
	 */
	int height;
	/**
	 * The command line, which a reload applies again.
	 */
	CommandLine arguments;
};

/**
 * Return whether the plugin is on in the choices: the Geometry,
 * Execution and Stopping plugins and the owned members.
 *
 * @param choices The choices of the run.
 * @param name The plugin's name, as `sampler/sobol`.
 */
bool chosenIn(const Choices& choices, const std::string& name);

/**
 * Return whether a `--use` key switches the plugin at run time.
 *
 * @param name The plugin's name, as `execution/packets`.
 */
bool switchedByUse(const std::string& name);

/**
 * Return the plugins a run's options choose: the Source, Transport,
 * Geometry, Execution and Stopping plugins, the owned members, then the
 * Reconstruction, Sink and Observer plugins.
 *
 * @param options The options of the run.
 */
std::vector<std::string> chosenPlugins(const Options& options);

/**
 * Return the Reconstruction plugins a run draws with by default:
 * `reconstruction/mean` alone.
 */
std::vector<std::string> defaultReconstruction();

/**
 * Read the command line into the scene's shot and the options, as both
 * programs read it: the settings (readSetting), the choices
 * (readChoice), `--use source=...`, `--use sink=...`,
 * `--use observer=...` and `--use reconstruction=...`, --out, --pfm,
 * --time, --reconstruction and --factor, and for the viewer --internal
 * w h; other flags are skipped.
 *
 * Without `--use source=`, the renderer draws the scene's frames, the
 * still or the frames of its clip (`source/scene`), and the viewer the
 * same with the window's camera on top (`source/scene`, then
 * `source/flight`). Without `--use sink=`, the renderer writes a still
 * to a file (`sink/file`) or a clip's frames to a directory
 * (`sink/frames`); a file with a clip is a still where --time is given.
 * The viewer shows the frames in its window (`sink/window`) and writes
 * them where --out is given. Without `--use observer=`, the renderer
 * draws its progress line (`observer/progress`) and the viewer has no
 * observer; `observer=metrics+progress` writes the metrics report too.
 *
 * A frame of the renderer, of a still or of a clip, is drawn until its
 * Stopping member is done. A frame of the viewer draws one sample a
 * pixel, unless `--use stopping=confidence_interval` asks for frames
 * drawn until the tolerance.
 *
 * The viewer draws with half the cores, Meet and Aim, the temporal
 * reconstruction with the upscaler, and frames drawn at the internal
 * size, which the shot's render size becomes (1 to 4 times smaller than
 * the output size on both sides, else it exits 1). The renderer draws
 * at the render size over --factor. A wrong value prints what the flag
 * takes and exits 1.
 *
 * @param file The scene, whose shot the settings override.
 * @param arguments The command line: the program, the scene file's
 * path, then the flags.
 * @param windowed Whether the viewer reads it.
 */
Options readOptions(SceneFile& file, const CommandLine& arguments, bool windowed);

/**
 * Return the time --time names, 0 where the command line names none:
 * the time a program loads the scene file at.
 *
 * @param arguments The command line.
 */
double timeArgument(const CommandLine& arguments);

/**
 * Return a scene loaded again with the command line's settings applied
 * again and drawn at the options' drawn size, whatever size the file
 * now names.
 *
 * @param loaded The scene as loaded.
 * @param options The options of the run.
 */
SceneFile reloadedFile(SceneFile loaded, const Options& options);

} // namespace kodanuki
