# Rules

Every code and format rule of kodanuki. When a detail is not covered
here, ask the owner before writing the code, then add the answer as a
new numbered rule in the same commit as the code it shaped. Numbers
are never reused, so a citation of a rule always means the same rule;
a dropped rule is deleted.

## Scope

1. The CPU is one backend (`backend/cpu`); a GPU backend may come as
   a plugin of the Backend point. The renderer draws offline frames,
   and a viewer (`kodanuki_view`, see 48) draws a sequence
   interactively in a window.
2. The core is tiny: types, steps, points, plugins, workers, the loop.
   Plugins do all the work and may be large. The core renders nothing
   on its own.
3. No dependencies beyond the C++ standard library for the core and
   the CPU plugins. `sink/window` links SDL3; a GPU backend links
   Vulkan and needs a GLSL compiler at build time. The build skips a
   plugin whose dependency is missing, and the program that needs it
   (`kodanuki_view` without SDL3). The scripts under `target/` use the
   Python standard library only.
4. Colour is spectral (hero wavelengths), with dispersion, thin films
   and media.
5. Data between steps is float where the float study allowed it
   (rule 55); a CPU kernel may use double inside, and transport and
   shading compute in double. The walk's packet maths uses float. The
   tree build and transforms stay double.

## Code

6. C++26. Plain headers with `#pragma once` and sources; no modules.
7. Structs and free functions only, no classes. Functional style.
   The one exception: `math::FloatVector`, `FloatSpectrum` and
   `FloatColour` (rule 55; all three in `math`) carry a
   converting constructor and a conversion operator, so a value
   converts where it is stored and read.
8. Functions are short: extract what makes a function read as one
   step. Complexity counts, not length: a long flat `switch` is fine.
   Small is not an absolute; readability counts more (see 44, 45).
10. No `inline`. Definitions live in `.cpp` files; link-time
    optimisation inlines. Headers hold structs and declarations.
    Exceptions: `source/plugins/execution/packets/packet.cpp`
    marks its node step's helpers `[[gnu::always_inline]] inline` (GCC
    wants the `inline` beside the attribute), since GCC otherwise keeps
    the child step out of line and returns its span through the stack;
    the conversions of `math::FloatVector`, `FloatSpectrum` and
    `FloatColour` (rule 7) are defined in their headers; the
    arithmetic of `math::Vector` (`source/math/vector.h`) is `inline`
    in its header; and `chainHyper2` in `source/sdf/numbers.cpp` is
    `[[gnu::always_inline]] inline`.
11. Templates only where nothing plainer works.
12. Conventional, PBRT-style names (sample, pdf, intersect).
    Comments: documentation in headers only, see 36 and 43.
13. Indent with tabs. Aim for about 80 columns, but this is not a hard
    rule: never split a line awkwardly just to fit.
    `target/.clang-format` applies the layout
    (`clang-format -style=file:target/.clang-format -i`); run it on
    every change. It keeps empty lines, so those inside a function are
    removed by hand (38).
14. Braces on the same line (K&R), functions included.
15. `camelCase` for functions and variables, `Capital` for types:
    `struct Hit`, `Hit intersect(const Tree& tree, const Ray& ray)`.
16. Every `if`/`for`/`while` body has braces, even a single statement.
17. `else` is banned. Use early returns, a ternary or a `switch`.
18. Types are spelled out; `auto` only for iterators and lambdas.
    Locals that do not change are `const`. `auto` also stands where the type is long and plain from the right-hand
    side or the loop: iterators, structured bindings
    (`for (const auto& [name, value] : named)`) and range-for elements.
19. No exceptions. The syntax layer
    returns its first error as a value, `file:line:column: message`
    (`LoadedScene.error`), and stops its work; the program (`render.cpp`,
    the tools) prints it and exits 1, a viewer shows it. Errors outside
    the syntax layer print their message and exit(1).
20. Everything is in `namespace kodanuki`, with sub-namespaces where
    they make sense.
25. Numbers, bools and enums are passed by value; everything else by
    `const T&`. Results come back as return values, not out-parameters.
27. Includes: the file's own header, then project headers, then `<std>`
    headers, each group sorted, with an empty line between groups.
28. A header holds its enums first, then its structs, then its
    function declarations, each group ordered by readability (what a
    reader needs first comes first). A source file is bottom-up: helpers
    before their callers.
29. Helpers used by one file are not marked (no `static`, no anonymous
    namespace); every function name is unique across the project.
30. A function takes at most 3 parameters. More are bundled into a
    struct passed by `const&`, filled with designated initialisers.
    Output parameters (non-`const` references and
    pointers the function writes) do not count toward the 3.
31. A function's signature stays on one line, even past 80 columns.
32. Readability comes first: extract whatever makes a function read
    as one step.
33. Optimisations are kept, but behind a clear seam so more can be
    added later; nothing is optimised in a way that blocks extension.
35. `source/render.cpp` and `source/viewer.cpp` are the programs; they
    parse the command line and call `render()`. The core lives in
    `source/core/`, the foundation in `source/shared/`, `math/`,
    `sdf/` and `syntax/` (rule 57), and each plugin in
    `source/plugins/<point>/<name>/`, as `feature/clouds`
    (reference_architecture.md). A plugin depends on the core, the
    foundation, the plugin that owns its point and the plugins its
    `needed` hook names. The core names no plugin: the build generates
    the list (`everyPlugin()`); an owner names its members in one
    dispatch file. Each plugin has a `plugin.h`/`plugin.cpp` whose one
    function returns its `Plugin`, with its effect: `Exact` (the mean
    is the reference's, or within the scene's stated tolerance),
    `Consistent` (in the limit) or `Biased` (it changes the mean, in
    all or in part). Every header has exactly one source file of the
    same name. A file of only structs is cut too small: functions are
    small, files are not. Everything that is not the renderer's code
    or a scene lives in `target/` (rule 51).
34. Programs are evaluated in several number types by one interpreter
    over the type (a template, as rule 11 allows), and more may be
    added. Today's types include point, dual (gradient), interval,
    hyperdual (curvature) and interval hyperdual (bounds on the
    curvature, for the build's proofs).

## Scenes

21. Scene files are hand-written in the language of `reference_language.md` and
    use the extension `.sdf`.

## Tests

22. Tests stay minimal: one small check program per stage, no
    framework.
23. A test must be able to catch a real bug. Remove tests that check
    nothing meaningful. Never delete a meaningful test to make a change
    pass; fix the code or the expectation, and ask the owner when it
    is unclear which is wrong.
24. The scene language's decisions live in `reference_language.md`; an answer
    of the owner about the language goes there, not here.
36. Headers are documented (43); source files, tests and tools have no comments,
    neither `//` nor `/* */`: the code reads without them. Explanations
    an agent needs beyond that go into its own notes, outside the
    repository.
37. No abbreviations in names (`lower`, not `lo`; `Expression`, not
    `Expr`; `Location`, not `Where`). Single letters from a formula
    (a, b, p, n, i, j, k, fa, d1) are fine. Inside `math` the usual
    mathematical names are fine (sin, cos, atan2, sqrt, min, max).
38. The scene files' layout holds in C++ too: after `{` a new line;
    `=` aligned in runs of consecutive lines; enum values one per line;
    no empty lines inside a function; they stay only between definitions, declarations and
    include groups.
39. Designated initialisers always break, one field per line. A list
    of lists breaks one row per line, rows stay inline; a flat list
    stays inline. A braced initialiser is never a call argument: name
    it as a local first. Initialise with `T name = {...}`. A small
    primitive-like type whose fields are clear from the type alone
    (`math::Vector {x, y, z}`, a colour) may be initialised
    by position, `{a, b, c}`, inline.
40. Structs have no default member values, and every initialiser names
    every field (enforced by -Werror=missing-field-initializers), except
    the positional initialisers of primitive-like types that rule 39
    allows. Common values come from functions (`rootContext()`,
    `defaultRender()`).
41. `template <...>` stands on its own line above the function.
42. The number types and their functions live in `kodanuki::math`.
43. Every enum, struct, field and function in a header has a
    documentation comment in good grammar: one short line saying what it
    is, then any number of lines describing it in more depth, then one
    `@param name` line per parameter. No `@returns`: the description
    says what comes back. The headers alone are enough to understand
    the renderer.
44. A function reads gist first, so the reader may stop early: special
    cases leave by early returns at the top, then the main line follows,
    with the details in named helpers.
45. A function stays at one level of abstraction: no raw string or
    index work next to high-level dispatch; push the low-level part into
    a named helper.
46. Values are passed and returned by value by default, and copying is
    preferred. A mutable reference `T&`, or filling something in place,
    is allowed only where copying measurably costs performance; where
    it does not, rule 25 holds.
47. Documentation describes the present code, never its history: no
    "the old renderer", "ported from", "as before", "now" or "unlike".
    A comment says what the code is and does; where it came from belongs
    in the commit message and the notes.
48. The viewer is a second program, `source/viewer.cpp`, built as
    `kodanuki_view` and run by `make viewer`. Like `render.cpp` it
    reads the renderer's flags (`readOptions()`, core/options.h) and
    calls `render()`; its parts are the plugins `sink/window` (the
    window, its keys and the flight) and `source/flight` (the camera
    flown in it, read from `sink/window`). The renderer never chooses
    them; the tests may check their SDL-free parts. `--size` is the
    output size, the window's and the shown frames'; its own flag
    `--internal w h` sets the internal size, which frames are drawn at,
    one sample a pixel and no drawing at a fraction of it; without it
    the two are the same. `--size` must be k = 1, 2, 3 or 4 times
    `--internal` on both sides, else it prints `--internal: --size must
    be 1, 2, 3 or 4 times it on both sides` and exits 1. Each frame is
    drawn jittered at the internal size: its film plan has the factor k
    and the phase of its index in the Bayer order (`bayerFilm`), so
    drawn pixel (x, y) aims into output pixel (x k + column, y k + row),
    and every output pixel gets its own sample once in k² frames; a
    phase that changes is not a move. The G-buffer is traced at the
    internal size through those output pixels' centres;
    `reconstruction/temporal` and then `reconstruction/atrous` work at
    the internal size on those jittered pixels, and
    `reconstruction/upscale` turns the result into the output size,
    moving or still (biased, but it keeps converging). It draws with Meet
    and Aim alone, no Strategy plugin, unless `--use strategy=` says
    otherwise:
    `strategy/join`'s light pass reruns on every move and splats onto
    pixels the upscaler does not read. It flies: W, A, S, D move along
    the view and its right, Space and Ctrl along the up, Shift four
    times as fast, the mouse with its right button held turns the view,
    the wheel scales the speed, R loads the scene file again, Escape
    quits. A scene with a clip plays by the wall clock and loops (its
    parts `source/scene`, then `source/flight`): P pauses the time, the
    left and right arrow keys step one clip frame back and forward, and
    the camera stays the clip's until a key moves it.
49. No local variable that only shortens a name: write
    `coat.axis`, not `const math::Vector& axis = coat.axis;` followed
    by `axis`. A local names a computed value or a step. A local that
    only shortens a field path is inlined
    where it is used at most three times; from four uses on it may
    stay. A named element lookup (`scene.leaves[node.leaf]`) names a
    value and may stay.
50. Prefer the existing types where they fit the meaning: a point or
    a direction is a `math::Vector`, not `float[3]` or a fresh
    `std::array`; an operation on such a type lives with it (an area
    of two edges is `math::parallelogramArea`, not a light's helper).
    Raw float arrays stay only in the walk's packet maths (rule 5).
51. The `Makefile` names two variables, `SCENE` and `CONFIG`, and its
    targets `render`, `viewer`, `test`, `export` and `clean`
    each call `target/workflow.py <target> $(SCENE) $(CONFIG)`, which
    builds only the programs the target runs. A config
    is a TOML file in `target/configs/` with three blocks. `[settings]`
    takes `width` and `height` (`--size`), `tolerance`,
    `samples`, `batch`, `split`, `surfaces`, `collisions`, `seed` and `threads`
    (their flags; one left out keeps the scene's value), `faces` (`true`
    or `false`) and `smooth_glass` (`"exact"` or `"lobe"`). `[viewer]`
    takes what only the viewer reads: `internal_width` and
    `internal_height` (`--internal`). `[plugins]` names every plugin
    as `"point/name"`, `true` or `false`, at the default the source
    gives it. The workflow turns a plugin into the `--use` member it
    stands for (`KEYS`, `LOOP_KEYS`), or builds it off where it has no
    member (`BUILT`: its folder's `plugin.cpp` is left out and its code
    reads `pluginBuilt()`); a plugin with no switch at all (`ALWAYS`)
    refuses `false`. A config has no comments and its `=` is not
    aligned, and it lists every plugin. `render_final.toml` draws final
    frames and refuses plugins whose effect is `Biased`;
    `render_fast.toml` draws quick frames (tolerance 4) with every
    plugin at its default; `viewer_fast.toml` keeps them too and leaves
    out `strategy/join`, which the viewer does not use (48);
    `reference.toml` names the simplest exact plugin of every point,
    the reference for correctness. An unknown key is refused, and so
    is a plugin folder the workflow does not know. Everything
    generated goes to `output/`: the build to `output/build/`, or to
    `output/build-<names>/` for each set of plugins built off (named by
    their names without the point), frames to
    `output/<scene>.ppm` and `.pfm`, the portable zip to
    `output/kodanuki-<version>.zip`. `target/` holds the rest: the CMake
    file, `.clang-format`, the configs, the docs, the tests, the tools,
    the workflow and the files the zip carries (`target/export/`).

52. A `Plugin` holds plain function pointers (`Hooks`). A hook is
    called once per run, per frame or per batch, or at a pass's launch
    and fold once per walk or pixel; never once per ray or per vertex.
    Per-ray variation is a `switch` in the owner's dispatch file.
53. A step's hook fills the worker in place (`Worker&`): rule 46's
    exception, stated once for all steps.
54. A plugin reads its state through one accessor function; the cast
    from the core's slot (`State`, `stateOf()`) lives there and nowhere
    else.
55. What a step reads or writes holds no pointer and no container
    inside an item. Between steps a vector, a spectrum and a colour are
    kept as `math::FloatVector`, `FloatSpectrum` and `FloatColour`, a
    number as `float`; a kernel reads them as the double types. Every
    such struct and every block element (`Block`) is checked trivially
    copyable.
56. Only the core's step functions read a clock to time the steps
    (core/step.h). A plugin publishes counters (`published`); it times
    no step and prints no metrics. Writing a report or a progress line
    is an Observer's work.
57. The foundation is layered: `shared` (generic tools that are not
    mathematical: images, `inParallel()`) and `math` (mathematical
    tools: vectors, random numbers, spectra, colours) depend on
    nothing of kodanuki, not even on each other; `sdf` and `syntax`
    depend on them. `shared` knows nothing of the renderer: no
    `Renderer`, `Plugin`, `Worker`, walks or scenes; it is a small
    standard library of our own. Something moves into `shared` only
    when at least two places use it, never because it might be useful.
