# Architecture

kodanuki is a spectral path tracer for scenes written in its own SDF
language (reference_language.md). A tiny core says what a path tracer
is, as a loop of named steps; plugins do the work. A still, the frames
of a clip and the viewer are the same loop with different plugins.

## Repository

```
Makefile          SCENE and CONFIG; render, viewer, test, export, clean
scenes/           the scenes, .sdf, and standard.sdf
source/           the renderer: render.cpp, viewer.cpp, core/, plugins/,
                  and the foundation (shared, math, sdf, syntax)
target/           everything else (rule 51):
  CMakeLists.txt  the build; it generates the plugin list
  workflow.py     what the Makefile's targets run
  configs/        the plugins and settings a frame is drawn with, .toml
  docs/           index.md, reference_*.md, task_*.md
  tests/          the checks, one program; generic.cpp holds the
                  generic test of every point
  tools/          stats, benches, comparisons, within-noise.py
  export/         the Makefile and readme the portable zip carries
output/           everything generated: build*/, frames, the zip
```

`make render` builds and draws `SCENE` with `CONFIG`'s plugins and
settings into `output/<scene>.ppm` (graded) and `.pfm` (linear);
`make viewer` opens it in a window; `make test` runs the checks;
`make export` builds the renderer and viewer for any x86-64 with AVX2,
once for each set of plugins the configs build off, and zips them with
the scenes, the configs and the workflow into
`output/kodanuki-<version>.zip`; `make clean` deletes `output/`. Four
configs come with it: `render_final.toml` (final frames, no plugin
whose effect is Biased), `render_fast.toml` (quick frames at tolerance
4, every plugin at its default), `viewer_fast.toml` (the viewer) and
`reference.toml` (the simplest exact plugin of every point).

## Layers

Every dependency points downward:

```
render.cpp, viewer.cpp   parse the command line, call render()
plugins/<point>/<name>/  everything that does work
core/                    types, steps, points, plugins, workers, the loop
syntax                   lexer, parser, evaluator, compiler
sdf                      programs, number types, scene data
math                     vectors, frames, random numbers; wavelengths,
                         Spectrum4, RGB upsampling, CIE
shared                   grade, PPM, PFM; inParallel()
```

`shared` and `math` sit side by side at the bottom: neither includes
the other or anything of kodanuki; the split is only whether a tool is
mathematical (rule 57).

A plugin depends on the core, the foundation, the plugin that owns its
point and the plugins its `needed` hook names (as `execution/packets`
names `geometry/tree`). The core never names a plugin: the build globs
`source/plugins/*/*/plugin.h` and generates `everyPlugin()`. The core
renders nothing on its own; `target/configs/reference.toml` names the
simplest exact plugin of every point, and that set is the reference for
correctness. Performance is measured with the plugins a config chooses.

## The loop

`render()` (core/render.h) opens the renderer and runs its frames:

```cpp
while (nextFrame(renderer)) {     // Source plugins: time, scene, camera, what changed
	buildWorld(renderer);         // Geometry, where the scene changed
	prepareFrame(renderer);       // the frame's place in its sequence
	drawFrame(renderer);          // preparations, passes, reconstruction
	deliverFrame(renderer, reconstructFrame(renderer));  // Sink plugins
}
return closeRenderer(renderer);
```

A Sink with a `hostRenderer` hook (`sink/window`) keeps the main thread
and runs the frames on a thread of its own. `drawFrame` runs the
preparations whose inputs changed, then the pixel pass in rounds: a
Stopping plugin that judges noise draws a trial of every pixel first
and the rest after (two rounds), a fixed count draws one. Then the
surface pass where a Reconstruction plugin reads surfaces, the
Reconstruction plugins in order, and what the frame keeps for the next.

A pass runs on the workers (`onWorkers`, core/worker.h): every worker
takes tiles off one counter, opens each, traces it and places it into
the frame under one lock. Tracing a tile is the batch loop
(`tracedTile`):

```cpp
while (launchWalks(renderer, pass, worker)) {
	extendRays(renderer, worker);     // Execution: rays to hits
	arriveAtHits(renderer, worker);   // Transport: a hit becomes a vertex
	connectLights(renderer, worker);  // Transport: answered lines add light
	scatterWalks(renderer, worker);   // Transport: the next ray, or the end
	foldWalks(renderer, pass, worker);
}
```

A walk waits on one ray at a time, and a vertex's lines to light are
tested one after another, so a batch holds walks in different phases
(`WalkPhase`, core/batch.h); each Transport step takes the walks whose
answered ray belongs to it. `LoopStep` (core/step.h) names every step
of the frame and of a worker.

### Passes

A pass (`Pass`, core/worker.h) is a launch and a fold around the same
middle steps:

| pass | launches | folds into | lives in |
|---|---|---|---|
| pixel pass | the samples the Stopping plugin says are owed | each pixel's `Estimate` | core/frame.cpp |
| light pass | light paths | splats | `strategy/join` (pass.h) |
| surface pass | one camera ray a pixel, ended at the first hit | the G-buffer | core/surfaces.h |

## Points

A point is a place in the loop that plugins fill (`Point`,
core/plugin.h). The core calls the chosen plugin's hook through a
pointer, once per batch, frame or run.

| point | step or hooks | chosen | plugins | contract |
|---|---|---|---|---|
| Source | `nextFrame` | in order | `source/scene`, `source/flight` | sets the frame's time, scene and camera and what changed; false on quit |
| Geometry | `buildWorld` | one | `geometry/sphere_trace` (reference), `geometry/tree` | what rays meet; hits agree with sphere tracing within the tolerance |
| Stopping | `trialSamples`, `allottedSamples`, ... | one | `stopping/fixed_count` (reference), `stopping/confidence_interval` | how many samples a pixel draws and how many share a first vertex; pixels stay independent, the mean stays the reference's |
| Execution | `extendRays` | one | `execution/single` (reference), `execution/packets` | gives every waiting ray its hit; the frame stays within noise |
| Transport | `arriveAtHits`, `connectLights`, `scatterWalks`, the walk hooks | one | `transport/paths` | what a path does at its hits |
| Reconstruction | `reconstructFrame` | in order | `reconstruction/mean` (reference), `temporal`, `atrous`, `upsample`, `upscale` | the drawn frame and its history into the shown image; never changes the scene, the world or the preparations |
| Sink | `deliverFrame` | in order | `sink/file`, `sink/frames`, `sink/window` | hands the shown frame out |
| Observer | `observed`, `tallied` | in order | `observer/progress`, `observer/metrics` | is told of the steps; changes nothing |
| Backend | `openWorkers`, `hostWorkers`, `ranStep`, ... | one | `backend/cpu` | runs the workers' steps; the frame stays within noise |

A preparation is not a plugin of its own: it is the `prepareFrame` hook
of the plugin it serves (`sampler/sky_map`, `sampler/glowing_surfaces`,
`sampler/toward_mirrors`, `sampler/highlight_guide`, `strategy/join`'s
light pass). Its `Plugin.reads` names what it reads, and it runs again
on every Cut and on each change of what it reads: `Camera` on Moved,
`Lights`, `Looks` and `Shapes` on the change of that name (a
preparation that reads glowing surfaces reads `Looks`); `Plugin.after`
orders it after the preparations it reads.

### Owned points

A plugin may own points whose members run per ray or per vertex. The
owner keeps a `switch` over its members in one dispatch file, which
link-time optimisation inlines; each member is still a folder of its
own, a plugin with `Point::Owned`.

| owner | point | members |
|---|---|---|
| `geometry/tree` | Detail | `detail/focus_on_view`, `pilot`, `split_hidden_primitives`, `split_crowded_leaves`; by distance always |
| `geometry/tree` | Build | `build/split_beside_finer`, `cut_long_programs`, `face_slabs`, `shared_coefficients`, `shared_subtrees`, `faced_hits` |
| `transport/paths` | Strategy | `strategy/join`, `strategy/connect`; meet and aim always |
| `transport/paths` | Sampler | `sampler/sobol`, `highlight_guide`, `each_lamp_first`, `sky_map`, `glowing_surfaces`, `toward_mirrors` |
| `transport/paths` | Feature | `feature/absorption`, `atmosphere`, `clouds`, `coats`, `dispersion`, `path_rules`, `scattering`, `sky_layers` |

A Detail or Build member keeps hits within the tolerance (the
compression passes and face slabs change no bit; faced hits change
normals by float rounding of the face). A Strategy or Sampler member
keeps the mean. A Feature member adds scene content: language fields,
scene data and shading.

## Plugins

A plugin is one struct value (`Plugin`, core/plugin.h): its name (its
folder, as `execution/packets`), its point, its effect (`Exact`,
`Consistent` or `Biased`), its `Hooks` (plain function pointers, null
where it fills none), what its work reads, and the preparations it
runs after. Its folder holds `plugin.h`/`plugin.cpp`, whose one
function returns the struct, and any files of its own.

- **Choosing.** One build holds every plugin. `--use point=name` (or
  `name+name` for a point that takes several) chooses at run time; a
  config's `[plugins]` block is turned into those flags by the workflow
  (rule 51). `useKeys()` (core/options.h) lists the keys.
- **Building off.** A plugin whose code asks `pluginBuilt()` may be
  built off (CMake's `-DOFF`): its `plugin.cpp` is left out, so the
  registry does not list it, and its code takes the plain way. The
  configs build off `build/faced_hits`, `feature/clouds`, `feature/coats`
  and `feature/path_rules` this way.
- **State.** A plugin keeps typed state at three lifetimes, each held
  by the core as one opaque slot that only the plugin's own accessor
  casts (rule 54): the run's (`Renderer.states`, `keptState()`,
  `stateOf()`), kept across frames (`History.states`: a preparation's
  tables while nothing it reads changes, a Reconstruction's history),
  and the worker's (`Worker.walks`, the Transport's walks; `Worker.held`,
  a pass's tile). The Transport's state of a frame is
  `Renderer.drawing.transported`.

## Workers and backends

- A worker (`Worker`, core/worker.h) owns a pool of slots (`Pool`,
  core/batch.h: each slot's phase, kind, ray, hit and sample), the
  Transport's walk of each slot, the pattern registers, and what the
  pass holds of its tile. The steps fill it in place (rule 53).
- A Backend opens workers (`openWorkers`: how many and their pool
  size) and runs a worker's step (`ranStep`). `backend/cpu` opens one
  worker per thread (`--threads`) and runs a step by calling the
  chosen plugin's C++ kernel (`kernelOf()`). A backend that holds no
  kernel for a step returns false and the fallback backend
  (`fallbackBackend()`, `backend/cpu`) runs it. The fallback also hosts
  every chosen backend's workers on its threads (`hostWorkers`).
- Pass workers belong to the backends. A plugin's own work outside the
  passes may keep its own threads: the tree build, the pilot, the
  temporal reconstruction and the upscaler.

## Frames: still, clip, viewer

- A frame is one delivered image at one time through one camera, an
  instant. `nextFrame` sets the time (`FrameState.time`); the scene file
  is evaluated at it where it read `time` (`sceneAt()`, timed as the
  step Evaluate) and compared with the frame before's part by part,
  which sets the change flags (`Change`, core/history.h): the camera
  Moved, the lights, sky, clouds and stars Lights, the materials, coats
  and groups Looks, the shapes' programs, places and count Shapes; a
  cut of the clip or its wrap in the viewer is a Cut. A file that never
  read `time` is evaluated once.
- `buildWorld` builds the world again on Shapes and on Cut, from the
  scene's own camera at the frame's time, never the flown one, so a
  move of the camera alone builds nothing. The preparations redo what
  their flags name. Only a Cut drops the history; on Lights, Looks or
  Shapes the Reconstruction plugins keep it where the surface they saw
  stays, with the short cap and the tight clamp of a moving camera
  (`ShownFrame.moved`), and where the count of shapes differs they
  tell surfaces apart without the leaf (`ShownFrame.recounted`).
- **Still** (`source/scene`, `sink/file`): one frame at time 0 or
  `--time`, drawn until the Stopping plugin is done.
- **Clip** (`source/scene`, `sink/frames`): the frames of the file's
  `clip`, each drawn as the still of its time.
- **Viewer** (`source/scene`, `source/flight`, `sink/window`): frames
  until quit. The window plays the clip by the wall clock
  (`Hooks.playedClip`), and `source/scene` loops it; the camera flown
  in the window lies on top of the scene's.
- While nothing changes, each pixel's `Estimate` carries on into the
  next frame (`History.estimates`); a change starts it over. A
  denoised or upscaled still is the still with those Reconstruction
  plugins on; no plugin belongs to one program.

## Metrics

- The core runs every step through its step functions (core/step.h),
  which tell the observers the step, the plugin, the worker, how many
  items and how long (`StepEvent`). A step reads the clock only where
  someone is told of it (`Observing`).
- A plugin publishes named counters from its state (`published`,
  `Counter`); it times no step and prints nothing (rule 56).
- `observer/metrics` writes the report; `observer/progress` draws the
  progress line. With no observer on, a step costs one empty check.

## Data between steps

- **The scene:** the loader returns four plain structs together in a
  `SceneFile`, or its first error as a value (`LoadedScene`, rule 19):
  `Scene` (geometry: programs, arguments, leaves, nodes, objects),
  `Looks` (materials with their media and glows, coats, the groups
  rules name), `Lighting` (lights, sky, clouds, stars) and `Shot`
  (camera, render, grade). Every function takes the smallest of them it
  needs; there is no god struct.
- **The world:** what the Geometry plugin built (`World`, `Geometry`).
  `geometry/tree` keeps its `BuildContext` (demand, rough tree, scratch)
  apart from the `Tree` result, and packs subtrees as they finish.
- **Rays and hits:** `Ray` and `Hit` (core/batch.h) hold floats in a
  fixed layout; a kernel computes in double and rounds what it found.
- **Walks:** `transport/paths` owns `Walk`: the `Path` (throughput,
  wavelengths, hero, vertex), the member plugins' state beside it
  (`Companions`, as `GuideState` and `RuleState`), its lines to light
  and the place of the light walk it leads with in the worker's pool.
- **Flat data** (rule 55): what a step reads or writes holds no pointer
  and no container inside an item; between steps vectors, spectra and
  colours are `math::FloatVector`, `FloatSpectrum` and `FloatColour`.
  Each header says what stays double.
- **Blocks:** a `Block` (core/block.h) names a flat array the kernels
  read: the tree's arrays, the transport's finishes, lamps and sky, a
  preparation's table. `frameBlocks()` lists those of the frame being
  drawn, from each plugin's `blocks` hook. A backend's `uploadBlock`
  hook and `blockChanged()` exist; the loop sends no block to the
  backends.
- **Sequences:** `History` (core/history.h) holds what a frame leaves
  the next: the last `FrameRecord` (each pixel's light parts and first
  `Surface`, where a Reconstruction plugin reads them), each pixel's
  estimate since the last change, and the kept plugin states. A frame's
  `FrameStep` holds its index, lens, the lens before, its film plan and
  what changed.
- **Groups:** a pixel's samples run in groups that share a first
  vertex (`split`, reference_language.md). The group's first sample
  walks the camera ray, lights the vertex and waits there
  (`WalkPhase::Branching`); the launch then branches every sample of
  the group from a copy of it (`branchWalks`, `branchedFrom()`). The
  shared coordinates (pixel, lens, wavelengths, the first vertex's
  lights) are drawn by the group's place, the rest by the sample's
  index, and the weighing counts the first vertex's aims once a group
  (`sharedAim()`). The fold keeps the spread of the shared part apart,
  which is what the stopping chooses a pixel's split from.
