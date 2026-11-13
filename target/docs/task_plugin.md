# Adding or changing a plugin

Read `task_code.md` first, then the points in
`reference_architecture.md`.

## A plugin of a point

1. **Folder.** `source/plugins/<point>/<name>/` (rule 35), with
   `plugin.h` declaring one function `Plugin <point><Name>Plugin();`
   (the build finds it by that line) and `plugin.cpp` defining it. It
   depends on the core, the foundation, the owner of its point and the
   plugins its `needed` hook names.
2. **The struct.** Fill `Plugin`: the name (the folder), the point,
   the effect (`Exact`, `Consistent` or `Biased`), the hooks it fills
   from `noHooks()`, what it reads and the preparations it runs after.
   A hook is called once per run, frame or batch (rule 52); state goes
   through one accessor (rule 54); work that prepares a frame is a
   `prepareFrame` hook with `reads` naming what it reads: `Camera`,
   `Lights`, `Looks`, `Shapes`.
3. **A member of an owned point** (Detail, Build, Strategy, Sampler,
   Feature) has `Point::Owned` and is also a case of its owner's
   member enum and dispatch: `DetailMember` (geometry/tree/detail.h),
   `PassMember` (geometry/tree/pass.h), `StrategyMember`,
   `SamplerMember`, `FeatureMember` (transport/paths/strategy.h,
   sampler.h, feature.h). Its per-path state is a field of
   `Companions`.
4. **Switch.** Name it in the `--use` key of its point (`useKeys()`,
   core/options.cpp) and in `target/workflow.py`: in `KEYS` or
   `LOOP_KEYS` with the key, and in `OFF` where it is off by default;
   else, where it has no member, in `BUILT` (its code asks
   `pluginBuilt()` and takes the plain way when it is built off) or in
   `ALWAYS` (`false` is refused). The workflow refuses to run while a
   folder is missing from these tables.
5. **Configs.** Add it to every file in `target/configs/` at the
   default the source gives it; `render_final.toml` and
   `reference.toml` turn on no plugin whose effect is Biased, and
   `reference.toml` turns on no plugin that prepares the frame.
6. **Tests.** It must pass its point's generic test in
   `target/tests/generic.cpp` (below); extend the test to name it. A
   check that needs it built reads its constant, as
   `check(!faceSlabsBuilt || ...)`, so that `make test` passes with it
   built off too.
7. **Docs.** Name it in its point's row in `reference_architecture.md`,
   and in `reference_language.md` if a scene can see it.

## The generic tests

`checkGeneric()` draws a small scene with the reference set
(`reference.toml`'s plugins) and with `geometry/tree`, then checks:

- **Geometry:** `geometry/tree` with every Detail and Build member, and
  with each alone, meets what `geometry/sphere_trace` meets within the
  tolerance; its frame keeps the reference's mean.
- **Execution, Backend:** `execution/packets`, and a backend that owns
  no kernels (alone and beside `backend/cpu`), keep the mean.
- **Stopping, Sampler, Strategy:** each plugin changes the frame and
  keeps the mean, within 4.5 standard errors.
- **Reconstruction:** `reconstruction/mean` over a clip of 16 frames of
  one sample, the scene unchanged, keeps the mean of a still of 16
  samples; every Biased plugin runs and its frame stays finite.
- **Configs:** every config names every plugin, as above.

## A new point

A point is a value of `Point` (core/plugin.h), the hooks it fills in
`Hooks`, the chosen plugins in `Renderer` (core/render.h), the step
that calls them (through `frameStepBegun` and `frameStepEnded`, so the
observers see it), and a `--use` key. Its simplest exact plugin goes
into `reference.toml`, and its generic test into `generic.cpp`. Ask the
owner first: the core stays tiny (rule 2).
