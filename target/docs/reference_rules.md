# Rules

Every code and format rule of kodanuki. When a detail is not covered
here, ask the owner before writing the code, then add the answer as a
new numbered rule in the same commit as the code it shaped. Numbers
are never reused, so a citation of a rule always means the same rule;
a dropped rule is deleted.

## Scope

1. The renderer is CPU only, no GPU. It draws offline frames, and a
   viewer (`kodanuki_view`, see 48) draws a sequence interactively in
   a window.
2. The core stays minimal; the extras may be large.
3. No dependencies beyond the C++ standard library, except that the
   viewer links SDL3; the build skips the viewer where SDL3 is missing.
   The scripts under `target/` use the Python standard library only.
4. Colour is spectral (hero wavelengths), with dispersion, thin films
   and media.
5. Transport and shading use double; the walk's packet maths uses
   float. The tree build and transforms use double.

## Code

6. C++26. Plain headers with `#pragma once` and sources; no modules.
7. Structs and free functions only, no classes. Functional style.
8. Functions are short: extract what makes a function read as one
   step. Complexity counts, not length: a long flat `switch` is fine.
   Small is not an absolute; readability counts more (see 44, 45).
10. No `inline`. Definitions live in `.cpp` files; link-time
    optimisation inlines. Headers hold structs and declarations.
    The one exception: `source/extras/execution_correct_waves/packet.cpp`
    marks its node step's helpers `[[gnu::always_inline]] inline` (GCC
    wants the `inline` beside the attribute), since GCC otherwise keeps
    the child step out of line and returns its span through the stack.
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
35. `source/render.cpp` and `source/viewer.cpp` are the programs (the
    viewer's parts live in `source/viewer/`, rule 48); the core lives in
    `source/<stage>/`, a folder per stage (reference_architecture.md), and each
    extra in `source/extras/<point>_<category>_<name>/`: its main
    extension point (strategy, sampler, preparation, detail, build,
    feature, stopping, execution, reconstruction), then correct, consistent, biased
    (it changes the mean) or mixed (part correct, part biased, as the
    coats: exact colour, tint and glow, laddered roughness; the category
    need not be exact), then its name, as in
    `feature_biased_clouds`. An extra depends on the core only;
    the core includes an extra only in its extension point's dispatch.
    Every header has exactly one source file of the same name. A file
    of only structs is cut too small: functions are small, files are
    not. Everything that is not the renderer's code or a scene lives in
    `target/` (rule 51).
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
    (`math::Vector {x, y, z}`, `AirPoint`, a colour) may be initialised
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
48. The viewer is a second program, `source/viewer.cpp` with its
    parts in `source/viewer/`, built as `kodanuki_view` and run by
    `make viewer`; it takes the renderer's flags (the shared readers in
    `frame/run.h`). The renderer never includes it; the tests may check
    its SDL-free parts. `--size` is the output size, the window's and
    the shown frames'; its own flag `--internal w h` sets the internal
    size, which frames are drawn at, one sample a pixel and no drawing
    at a fraction of it; without it the two are the same. `--size` must be
    k = 1, 2, 3 or 4 times `--internal` on both sides, else it prints
    `--internal: --size must be 1, 2, 3 or 4 times it on both sides`
    and exits 1. Each frame is drawn jittered at the internal size: its
    film plan has the factor k and the phase of its index in the Bayer
    order (`bayerFilm`), so drawn pixel (x, y) aims into output pixel
    (x k + column, y k + row), and every output pixel gets its own
    sample once in k² frames; a phase that changes is not a move. The
    G-buffer is traced at the internal size through those output
    pixels' centres; the temporal accumulation (Temporal) and then the
    denoiser (Denoise) work at the internal size on those jittered
    pixels, and the temporal upscaler (Upscale) turns the result into
    the output size, moving or still (biased, but it keeps converging).
    It draws with Meet and Aim
    unless --use strategies says otherwise: Join's light pass reruns on
    every move and splats onto pixels the temporal upscaler does not
    read. It flies: W, A, S, D move along the view and its right, Space
    and Ctrl along the up, Shift four times as fast, the mouse with its
    right button held turns the view, the wheel scales the speed, R
    loads the scene file again, Escape quits.
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
    `samples`, `batch`, `surfaces`, `collisions`, `seed` and `threads`
    (their flags; one left out keeps the scene's value), `faces` (`true`
    or `false`) and `smooth_glass` (`"exact"` or `"lobe"`). `[viewer]`
    takes what only the viewer reads: `internal_width` and
    `internal_height` (`--internal`). `[extras]`
    names every extra by its folder name, `true` or `false`, at the
    default the source gives it. The workflow turns an extra into the
    `--use` member it stands for, or builds it off where it has no member;
    an extra with no switch at all refuses `false`. A config has no
    comments and its `=` is not aligned, and it lists every extra.
    `render_final.toml` draws final frames and leaves the biased and
    mixed extras out; `render_fast.toml` draws quick frames (tolerance
    4) with every extra at its default; `viewer_fast.toml` keeps them too
    and leaves out `strategy_correct_join`, which the viewer does not
    use (48).
    An unknown key is refused, and so is an extra folder the workflow
    does not know. Everything
    generated goes to `output/`: the build to `output/build/`, or to
    `output/build-<names>/` for each set of extras built off (named by
    their names without point and category), frames to
    `output/<scene>.ppm` and `.pfm`, the portable zip to
    `output/kodanuki-<version>.zip`. `target/` holds the rest: the CMake
    file, `.clang-format`, the configs, the docs, the tests, the tools,
    the workflow and the files the zip carries (`target/export/`).
