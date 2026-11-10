# Architecture

kodanuki is a spectral path tracer for scenes written in its own SDF
language (reference_language.md). It draws offline frames, and a viewer
(`source/viewer/`, rule 48) drives a sequence in a window.

## Repository

```
Makefile          SCENE and CONFIG; render, viewer, test, export, clean
scenes/           the scenes, .sdf, and standard.sdf
source/           the renderer: the programs, a folder per stage, extras/
target/           everything else (rule 51):
  CMakeLists.txt  the build
  workflow.py     what the Makefile's targets run
  configs/        the settings a frame is drawn with, .toml
  docs/           index.md, reference_*.md, task_*.md
  tests/          one check program per stage
  tools/          stats, benches, comparisons
  export/         the Makefile and readme the portable zip carries
output/           everything generated: build*/, frames, the zip
```

`make render` builds and draws `SCENE` with `CONFIG`'s settings into
`output/<scene>.ppm` (graded) and `.pfm` (linear); `make viewer` opens
it in a window; `make test` runs the checks; `make export` builds the
renderer and viewer for any x86-64 with AVX2, once for each config's
extras, and zips them with the scenes, the configs and the workflow into
`output/kodanuki-<version>.zip`; `make clean` deletes `output/`. Three
configs come with it: `render_final.toml`, final frames without the
biased and mixed extras; `render_fast.toml`, quick frames at tolerance 4
with every extra; and `viewer_fast.toml`, the viewer with them.

## Layers

The **core** is minimal. It defines how the SDF renderer works and renders a
correct, unbiased image on its own, one ray at a time. **Extras** do all the
heavy lifting, for speed and for quality, and plug into named **extension
points** that the core owns. Performance is always measured as core plus
extras; the core alone is the reference for correctness, not for speed. The
geometry (tree, patches, exact faces, walk) is core. Every dependency points
downward:

```
render.cpp, viewer.cpp  the programs (viewer/ holds the viewer's parts; it alone links SDL3)
frame       tiles, one or many paths per sample, byte-identical merge, image hand-off, sequences
transport   paths, vertices, the MIS engine over strategies
light       the five lights, gradient sky
material    matte, plastic, metal, glass; microfacet, Fresnel, energy tables
walk        scalar walk over the packed tree, faces and patches, SDF at hits
tree        demand, grow, classify, patch fit, exact faces, pack
syntax      lexer, parser, evaluator, compiler (kodanuki's language)
image       grade, PPM, PFM
sdf         programs, number types, scene
spectral    wavelengths, Spectrum4, RGB upsampling, CIE
math        vectors, frames, random numbers
```

Extras live in `source/extras/<point>_<category>_<name>/`, one header/source
pair or more. The point is the extra's main extension point; the category
is `correct` (the mean is the core's, or within the scene's stated
tolerance), `consistent` (correct in the limit), `biased` (it changes the
mean) or `mixed` (some of what it adds is correct and some biased; the
category need not be exact). An extra may depend on the core and on
nothing else, and the core never includes an extra's header except in the
one dispatch file of its point.

A config's `[extras]` block switches every extra by its folder name,
such as `feature_biased_clouds = false`. Most extras are a `--use` member
and switch at run time; the others are built off: such an extra holds a
`KODANUKI_OFF_<FOLDER>` switch in its header, and the build refuses to
build off an extra without one. The biased, mixed and consistent extras
have one: `feature_biased_clouds` off marches every ray through the clouds,
`feature_mixed_coats` off keeps every material's own roughness,
`feature_biased_path_rules` off ignores the scene's rules, and
`stopping_consistent_confidence_interval` off takes the fixed count. So
does `build_correct_face_slabs`, a pure optimisation: off, the tree keeps
no slabs and every face is solved; and `build_correct_faced_hits`: off,
runs carry no pass words and every hit evaluates its leaf's programs.

## Extension points

Each point is one core header holding its query and result structs, a
`Member` enum and one dispatch function whose `switch` calls into the extras.
An extra brings its own typed state struct, and the frame holds it in a
`Prepared` struct with one field per extra. There are no opaque buffers,
link-time seams or virtual calls.

| point | lives in | what plugs in | contract (checked by one generic test per point) |
|---|---|---|---|
| Strategy | transport | meet and aim (core); join, BDPT connections | builds paths and reports its density for every path; the MIS engine weighs all strategies; the mean never changes |
| Sampler | transport | sequence (independent; Sobol), direction (material; highlight guide), light selection, emission (cosine; toward mirrors), sky map, glowing surfaces | draws and reports its pdf; the mean never changes |
| Preparation | frame | guide survey, sky chart, glow cells, mirror lamps, light pass | runs once per frame and returns its typed state; a sequence keeps the Load ones until the scene or lights change and the Camera ones until anything changes |
| Detail | tree | by distance (core); pilot, focus on view, split rules (hidden primitives, crowded leaves) | says how fine a cell must be; hits stay within tolerance |
| Build pass | tree, walk | program cutting, shared coefficients, shared subtrees, split beside finer, face slabs (a slab after every curved face, which the leaf solve asks per face through the point's dispatch before it solves the face), faced hits (words after a run's table: the program each face comes from alone, and the faces that are the field's own surface with the scene leaf they paint; a hit asks the point's dispatch for the face it landed on and takes that program alone, or the face's gradient and leaf without evaluating any program) | rewrites the tree; hits stay within tolerance (the compression passes and face slabs change no bit; faced hits change normals by float rounding of the face); memory is reported |
| Feature | material, light, sky, sdf | coats, thin film, media and scattering, dispersion, clouds, atmosphere, moon, stars, painted sky, path rules, lattice, material switches | adds scene content: language fields, scene data and shading |
| Stopping | frame | fixed count (core); confidence interval | decides when a pixel is done and how many of its samples share a first vertex; pixels stay independent |
| Execution | frame, walk | one path at a time, and the G-buffer one ray at a time (core); the G-buffer in 8-ray packets; waves with ray queues and the 8-wide packet walk, bare shadow rays, missed lanes (the lanes that surely meet nothing in a surface leaf found eight at once), grouped hits (the programs of a leaf several lanes met read once for all, eight points a step) | runs the same paths in another order; the frame stays byte-identical to the core's, and so do a sequence's records |
| Reconstruction | frame | the running mean (core, Mean), bilinear upsampling (Upsample), a temporal accumulation (Temporal), an à-trous denoiser (Denoise), a temporal upscaler (Upscale) | turns a sequence's drawn frames and its history into the shown image; never changes the scene, the tree or the preparations; a still frame runs none. With a G-buffer (drawn pixel (x, y)'s first surface through the centre of the shown pixel its phase aims at) Temporal and Denoise work at the drawn (internal) size on the jittered pixels, and Upscale, last, blends that image into a history at the shown (output) size |

The generic tests:
- **Strategy and Sampler:** render a small scene with the member off and on, and check that the means agree within the standard error.
- **Detail and Build pass:** compare hits against sphere tracing within the tolerance.
- **Stopping:** check unbiasedness against a fixed count.
- **Reconstruction:** N still frames with Mean equal the still frame of N fixed samples, to the byte, with either Execution member.

`--use name=member` selects members for comparisons. Only about 8 switches
are exposed (integrator strategies, highlight guide, light selection,
detail members, compression, exact faces); everything else is the default
code path.

## Data between stages

- **The scene:** the loader returns four plain structs together in a `SceneFile`, or its first error as a value (`LoadedScene`, rule 19): `Scene` (geometry: programs, arguments, leaves, nodes, objects, objectPrograms), `Looks` (materials with their media and glows, coats, the groups rules name), `Lighting` (lights, sky, clouds, stars) and `Shot` (camera, render, grade). A leaf keeps its material index into `Looks`. Every function takes the smallest of them it needs; only the programs, the tests and `featureQueryOf` see the whole `SceneFile`; there is no god struct.
- **The tree:** `BuildContext` (demand, rough tree, scratch) is separate from the `Tree` result. The build streams: subtrees are packed as they finish.
- **The frame:** `Prepared`, one typed field per Preparation member.
- **Sequences:** a frame of a sequence (`steppedFrame`, frame/sequence.h) draws one sample a pixel, of the frame's index, and reads its `FrameStep` (index, lens, lens before, film plan, what changed) and the `History` of the frame before; it returns the shown image and the next `History`: the frame's `FrameRecord` (each sample's `Parts` and first `Surface`), each pixel's `Estimate` since the last change, and the kept preparations. Both are plain values the caller owns; nothing is global. A still frame (`renderFrame`) builds no history, records nothing (`Tracing.recording` off, `stillFilm()`) and runs no Reconstruction member.
- **Paths:** `Path` holds the core state only (throughput, wavelengths, hero, vertex); `GuideState` and `RuleState` are separate structs owned by their extras.
- **Groups:** a pixel's samples run in groups that share a first vertex (`split`, reference_language.md). The group's first sample walks the camera ray, lights the vertex and waits there (`WalkPhase::Branching`); `branchedFrom()` then takes every sample of the group on from a copy of it. The first sample carries what the shared stretch gathered once for each sample of the group, so every Execution member still folds one colour a sample, in index order. The shared coordinates (pixel, lens, wavelengths, the first vertex's lights) are drawn by the group's place, the rest by the sample's index, and the weighing counts the first vertex's aims once a group (`sharedAim()`). The fold keeps the spread of the shared part apart, which is what the stopping chooses a pixel's split from.
- **Precision:** transport is double; the walk's packet maths is float (rule 5).

