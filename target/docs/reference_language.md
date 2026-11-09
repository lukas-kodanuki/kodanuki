# The scene language

A scene is one `.sdf` file, written by hand. It is C-like: `//` and
`/** ... */` comments (any `/* ... */`, not nested), `;` ends a
statement, braces make a block. It runs once when it loads. What it leaves behind is a flat list of shapes, each a
point function placed and painted, plus the camera, lights, sky and
settings. Space is z-up, lengths are metres, angles are radians.

Draft for milestone 1. "Open" marks what is still to be decided.

## Values

| Value | Written | Notes |
| --- | --- | --- |
| number | `1`, `0.5`, `1e-3`, `pi` | one kind of number |
| list | `[1, 2, 3]`, `[[0, 0], [0.2, 0.05]]` | one kind of element; a list of numbers is a vector |
| colour | `#C41E3A` | a list of three numbers, sRGB turned linear; `[r, g, b]` is linear |
| material | `plastic(#FFFFFF, roughness = 0.7)` | see Materials |
| name | `x`, `filmic` | an axis or a setting's word |

Lists are vectors. `+ - * /` work element-wise and a number spreads
over a list. `a[i]` indexes, `count(a)` counts, and `.x .y .z .w` and
swizzles such as `.xz` or `.zyx` read the first four elements. A list
that mixes kinds (`[1, [2, 3]]`) is an error. There are no strings
except the path of an `import`.

Booleans are numbers: comparisons `< <= > >= == !=` give 0 or 1, and
`&& || !` treat 0 as false.

## Statements

```
import "standard.sdf";           // definitions of another file, by path from this one
let size = 0.5;             // a binding; `let size = 0.6;` changes it
for i in 0..4 { ... }       // 0, 1, 2, 3
for x in 0..3, y in 0..3 { ... }   // nested, x outermost
for square in board { ... } // each element of a list
if a < b { ... }            // no else (rule 17); use select(c, a, b)
```

Every binding and every change of one is written with `let`; there
is no bare `x = ...`. A `let` of a name already bound in this block or
one around it changes that binding, so a loop can carry a value out:
`let d = min(d, e);`. A new name lives until its block ends.

## Point functions: `function`

```
function sphere(r) { return length(p) - r; }
```

A `function` is a function of a point. Inside it `p` is the point (a
vector of three), and when it is used as a pattern `n` is the surface
normal. It must `return` a number. Used as a shape, the number is a
distance. Used as a pattern, it is a value in 0..1 for a ramp. Both
kinds are the same thing.

A shape's distance must never be larger than the true distance
(standard SDFs are fine). The builder relies on this to drop a shape
from cells it cannot reach. A displaced shape must be scaled down by
hand to keep to it.

The body may use `let`, `for` and `if`. Every loop bound
must be known when the scene loads: a literal, or `count` of a list
argument. Loops are unrolled, and `if` becomes a select. A `function` is
compiled once per distinct set of list arguments; its number
arguments stay arguments, so a thousand spheres share one program.

Built-ins: `sqrt abs floor sin cos atan2 pow min max clamp mix
length dot cross normalize select all any sign`, and `pi`.

## Builders: `shape`

```
shape pawn() {
	merge(0.02) {
		revolve(PAWN);
		at([0, 0, 0.45]) sphere(0.095);
	}
}
```

A `shape` runs at load time and emits shapes. It may call fns
(emitting one shape), call other shapes, loop and branch. It returns
nothing, and it may emit nothing. It sees the bindings at the top
level of the file but not its caller's, and a `let` of a top-level
name changes it, so a shape can carry a value out (scenes/city.sdf
draws its random numbers so):

```
let seed = 49534;
shape draw() {
	let seed = seed * 16807;
	let seed = seed - floor(seed / 2147483647) * 2147483647;
}
```

## Emitting a shape

A statement that calls a `function` or a `shape` emits it. Modifiers before
it place and paint it. They read left to right, the outermost first:

```
paint(gold) at([0, 0, 1]) turn(z, pi / 4) box([1, 1, 1], 0.05);
```

| Modifier | Does |
| --- | --- |
| `at(v)` | moves by v |
| `turn(axis, angle)` | turns about `x`, `y`, `z` or a vector, right-handed |
| `scale(s)` | a number or a vector; a negative element mirrors |
| `paint(m)` | the material of everything under it that has none closer |
| `coat(effect, mask)` | a change of its look where the mask says (see Coats) |
| `rule(camera = 0, shadows = 0, caustics = 0)` | rules the paths that meet it obey (see Rules) |
| `group(name)` | puts it in a named group, which a light may reach alone |

For a block, the chain starts with `with`:

```
with paint(black) at([0, 0, 2]) {
	box([0.5, 0.5, 0.5], 0.05);
	sphere(0.1);
}
```

Statements side by side are merged. Inside a `shape` that is a hard
merge; at the top level it means a separate object. The difference
matters only for smooth joins.

## Joins

```
merge { ... }        merge(0.02) { ... }   // smooth with radius 0.02
cut { base; a; b; }  cut(0.005) { ... }    // base minus the rest
clip { a; b; }       clip(0.01) { ... }    // what all of them share
```

A join is a statement, so modifiers prefix it without `with`:
`at([0, 0, 0.56]) merge { ... }`.

A hard union at the top of an object whose program has 512 steps or
more is cut into objects of its own, one per operand, and an operand
that is again such a union is cut the same way. The field is the same
either way; a cell of the tree then holds only the parts near it.
Smooth joins, cuts and clips keep their operands in one object.

## Repeats

```
repeat(count = [3, 1, 4], step = [2.6, 0, 3.3]) {
	at([0, -0.12, 0]) paint(pane) box([1.45, 0.24, 2.1], 0);
	paint(interior) box([3, 3.2, 2.4], 0);
}
```

A `repeat` runs its block once and copies the shapes it emits
`count.x × count.y × count.z` times; copy (i, j, k) is moved by
`i · step.x` along the block's x axis, `j · step.y` along y and
`k · step.z` along z, in the block's own space after the modifiers
before it. Anything the block draws or changes, such as a random number
or a binding, happens once, so every copy is alike; a `for` loop runs
its body once per pass instead. A count is a whole number of one or
more, and a number spreads over all three. Like a join, a `repeat`
takes modifiers without `with`, and its copies stand side by side as
statements do.

## Materials

```
let red   = plastic(#C41E3A, roughness = 0.7);
let steel = metal([0.9, 0.9, 0.9], roughness = 0.25);
let floor = matte(ramp(checker(4), #5C4636, #C9B89A));
let lens  = glass(refraction = 1.5, absorb = [0.1, 0.3, 0.2], roughness = 0);
let neon  = matte(#000000, glow = #FF2080, strength = 8);
```

The kinds are `matte(albedo)`, `plastic(albedo, roughness)`,
`metal(albedo, roughness)` and `glass(refraction, absorb, roughness)`. Every kind
takes `glow` and `strength`, which makes the surface a light. A glass of
roughness 0 is a true mirror and an exact passage (`--use
smooth_glass=lobe` makes it the narrowest GGX lobe instead, for
comparisons).

A glass also takes `dispersion`, Cauchy's B in square micrometres: its
index at a wavelength is `refraction + dispersion / λ²`, λ in
micrometres (crown glass about 0.004, dense flint about 0.01). A path
bent through a dispersing surface keeps only one of its wavelengths
from there on. Only a glass takes `dispersion`.

A glass's body may also turn light aside: `scatter` is how much of a
metre of travel it turns aside (per metre, at every wavelength alike,
default 0), and `lean` how forward it turns it, from -1 (back) through
0 (evenly every way, the default) to 1 (onward), Henyey-Greenstein's
g. Milk, jade or smoke are such bodies. Only a glass takes `scatter`
and `lean`. A glass of `refraction = 1` without dispersion or film,
of any roughness, bends nothing: its surface is only the boundary of
its body, as of a fog, which paths and the lines to the lights cross
as if it were not there, and a path aims at the lights from where it
turns aside inside it.

A metal, a plastic or a glass takes `film = [thickness, index]`: a
layer `thickness` nanometres deep of that index, thin enough that light
off its two faces interferes, as on a soap bubble or an oil slick. On a
metal it lies over the facets, on a plastic it is the coat, and on a
glass it is the boundary (a path through it keeps one wavelength).

Every kind also takes three switches, each 1 (the default) or 0, which
change how much noise a frame keeps and never what it shows: `guided`
(the highlight guide is built on the surface), `relays` (paths from the
lights are drawn toward it, where it is sharp) and `aimed` (paths aim
at its glow rather than only meeting it).

An albedo is a colour or a ramp: `ramp(pattern, c0, c1, ...)` spreads
the colours evenly over 0..1. The pattern is a call of a function,
`checker(4)`, read in the painted shape's own space. `world(checker(4))`
reads it in world space instead. A pattern may read the normal `n`.

## Coats

A coat changes how a surface looks where a mask says, and nothing
about where the surface is. It is a
modifier, `coat(effect, mask)`, and the effect and the mask are
written in it:

```
coat(wet(0.8), below(0.3, soft = 0.1)) paint(stone) box([2, 2, 2], 0);
coat(gold, facing([0, 0, 1], level = 0.7)) paint(rock) sphere(1);
```

The mask weighs each point of the surface from 0 to 1:

| Mask | Worn |
| --- | --- |
| `above(height, soft = 0, axis = [0, 0, 1])` | past a height along a world axis |
| `below(height, soft = 0, axis = [0, 0, 1])` | under it |
| `facing(axis, level = 0.5, soft = 0.2)` | where the normal's cosine to the axis passes `level` (axis default `[0, 0, 1]`) |
| `inside(f, soft = 0)` | where the function `f`, read at the world point, is below 0 |
| `along(f, width, soft = 0)` | in a band `width` wide about the surface of `f` |
| a function call | its value clamped to 0..1, in the painted shape's space (or `world(...)`) |

`soft` is how far the edge takes to fade, half of it either side, by a
smooth step; 0 is a hard edge. Every mask but a function call also
takes `ragged` and `grain` (defaults 0 and 1): value noise of cells
`grain` metres across moves the edge by up to `ragged`, in the units
the mask's reach is measured in, for a torn or worn edge. The effect is
taken as far as the weight:

| Effect | Does |
| --- | --- |
| a material | mixes its albedo and glow in, and becomes it past a half |
| `tint(colour)` | multiplies the albedo |
| `shade(amount)` | darkens toward black (below 0, to -1) or lightens toward white (to 1) |
| `rough(amount)`, `polish(amount)` | move the roughness toward 1 or toward 0 |
| `wet(amount)` | halves the albedo and takes the roughness down by four fifths |
| `glow(colour, strength = 1)` | adds light |
| `bump(relief, amount, span = 1, along = [1, 0, 0])` | tilts the shading normal along the slope of a relief `amount` high |

Several coats are laid on innermost first: in
`coat(a, ...) coat(b, ...) sphere(1)` the sphere wears `b`, then `a`.
A roughness a coat changes is drawn by one of 17 copies of the
material at roughness 0, 1/16, ... 1, the two either side of it picked
by a hash of the point. This is the mixed extra `feature_mixed_coats`
(its colours, tints and glows are exact, its laddered roughness is not);
built off (reference_architecture.md), every material keeps its own roughness.

A bump's relief is `grain` (two octaves of value noise), `pits`
(hollows where the noise is high), `streak` (noise drawn out along the
axis `along`, as on brushed metal), each with cells `span` metres across
in world space, or a function call read as a height, in the painted
shape's space (or `world(...)`). The surface itself does not move; only
the normal it is shaded with tilts, and it never tilts away from the
ray. A bump may leave its mask out and is then worn everywhere:

```
coat(bump(grain, 0.02, span = 0.05)) paint(plaster) box([2, 2, 2], 0);
coat(bump(streak, 0.01, span = 0.02, along = [1, 0, 0])) paint(steel) cylinder(1, 0.3);
coat(bump(pits, 0.03, span = 0.1), above(0.5, soft = 0.1)) paint(stone) sphere(1);
coat(rust, facing([0, 0, 1], level = 0.3, soft = 0.1, ragged = 0.25, grain = 0.12)) paint(iron) sphere(1);
```

## Rules

A shape may say rules about the paths that meet it, each a switch of
`rule` that is 1 (the default) or 0. Inner rules override outer ones.
Rules change what the physics would draw: they are the biased extra
`feature_biased_path_rules`, and built off (reference_architecture.md) they are
ignored.

```
rule(camera = 0) paint(pane) box([2, 0.02, 1.5], 0);   // the camera sees through it
rule(shadows = 0) at([0, 0, 3]) sphere(0.2);           // it casts no shadow
rule(caustics = 0) paint(gold) sphere(1);              // no caustic through it or onto it
group(stage) paint(marble) box([4, 4, 0.2], 0);        // a shape of the group stage
```

- `camera = 0`: the camera's rays and the lines to the lens pass through
  the shape, as if it were not there; everything else sees it.
- `shadows = 0`: lines to the lights pass through the shape, and a ray
  that scatters toward it looks past it for the lights before it stands
  on it, so it casts no shadow, direct or indirect.
- `caustics = 0`: no caustic is focused by the shape or lands on it: a
  path that has passed a dull surface and then only sharp ones may not
  end on a light where one of those surfaces, or the dull one, says so.

A light may say `reaches = [names]`, the groups whose shapes it lights
alone (the empty list, the default, lights every shape), and
`caustics = 0`, so that no caustic ends on it. A group name is a word,
and a scene names at most 64 groups.

## Settings

Blocks of fields, one each, at the top level:

```
render { width = 1920; height = 1200; tolerance = 2; seed = 1; }
camera { eye = [3, 3, 4]; target = [0, 0, 2]; up = [0, 0, 1]; fov = 0.8;
         aperture = 0; focus = 0; }
grade  { exposure = 1; curve = filmic; }        // filmic | clamped
sky gradient { zenith = [...]; horizon = [...]; ground = [...]; }
sky physical { haze = 3; ground = [...]; }      // lit by the sun light
sky painted { day = [...]; dusk = [...]; night = [...]; }
clouds { height = 1500; thickness = 1200; cover = 0.45; density = 0.03; span = 3000; }
stars  { count = 3000; strength = 2; angle = 0.003; }
light directional { direction = [0.4, -0.5, 0.7]; power = [2.4, 2.3, 2.1]; }
light point  { at = [...]; power = [...]; }
light sphere { at = [-2.2, -1.6, 3]; radius = 0.3; power = [18, 17, 15]; }
light quad   { at = [...]; along = [...]; across = [...]; power = [...]; }
light sun    { direction = [...]; angle = 0.0093; power = [...]; }
light moon   { direction = [...]; angle = 0.0093; power = [...]; }
```

Every light also takes `reaches` and `caustics` (see Rules).

`render` also takes `far`, the reach of the automatic region (default
1000), `samples`, the most samples a pixel draws, `surfaces`, the most
surfaces a path stands on (default 10, at least 1; it scatters from one
fewer), and `collisions`, the most times a path turns aside inside
scattering bodies (default 256, at least 1). Collisions are counted
apart from surfaces: a path ends at its last surface or at its last
collision, whichever comes first, so a dense fog or milk needs no
deeper surface count.
`tolerance` is the noise a pixel may keep: a pixel draws samples until
1.96 standard errors of its mean span no more than twice `tolerance`
steps of a byte after the grade, on its worst channel. `batch` is how
many samples a batch holds: every pixel first draws a trial
of two batches, whose batch means give its noise, and its count is set
from what the trials of the eight pixels around it want, in whole
batches, at most `samples`. Where `samples` or `batch` is not given,
the tolerance sets it: one effort e = log(62.5 / tolerance) / log(125),
from 0 up to 2^0.66, gives batches of round(1 + 7e)^2 samples and at
most 32 x 512^e samples a pixel, so the default tolerance of 2 asks for
batches of 36 and at most 2732 samples (a tolerance of 0 or less: 64
and 4096). The region the tree is built over is found by sampling the
scene from the camera out to `far`. Shapes that never end, such as a
ground plane, are cut off at `far`.

The command line overrides every `render` field but `far`: `--size w
h`, `--tolerance t`, `--samples n`, `--batch n`, `--surfaces n`,
`--collisions n` and `--seed n` (surfaces and collisions below 1 are
taken as 1). `make render` draws the scene with the config's settings
into `output/`; rule 51 lists the settings a config takes.

A physical sky is the air over the scene lit by its first `sun`
light, scattered once, worked out per wavelength. `haze` is
how murky the air is, about 1 for mountain air and 10 for smog
(default 3); `ground` is the fraction of light the ground below the
horizon sends back. It reads no other fields, and a scene with a
physical sky must have a sun. The sun's `power` is what it sends above
the air; the air tints it on its way down.

A `sun` lies along `direction` (toward the sun) as a disc `angle`
across (radians, full angle), and `power` is the light it lands on a
surface facing it. A `directional` light is the same without a disc.
A `moon` is a disc like a sun, and `power` is what it lands when full:
only the part of its ball that faces the first sun shines, so it is
full opposite the sun and new beside it (in a scene without a sun it
is full).

A painted sky is three runs of colour over the sine of the elevation
(-1 straight down, 0 level, 1 straight up): `day`, shown with the
first sun above 12 degrees, `night`, below -14 degrees, and `dusk`
between them, shown toward the sun and shared out to day and night
away from it. A run is a list of two stops or more, each
`[height, r, g, b]` with the heights rising, and a run left out keeps
its default (a deep blue day, an orange dusk, a dark blue night). A
scene without a sun shows the day. A painted sky tints the first sun
and every moon: they fade as they sink below the horizon, and the sun
takes on the colour of the dusk horizon as it sets. It reads no other
sky fields but the glow's.

`stars` is a point of light in each of about `count` equal cells of
the sky, `angle` across (radians), of which the brightest sends
`strength`; most are far dimmer. They show only above the horizon, and
only as the first sun sets, from 2 to 12 degrees below the horizon (in
full in a scene without a sun).

Every sky takes `glow`, `strength` and `spread`: a glow about the
first sun of colour `glow` times `strength`, falling off as
exp(-angle / spread) from it (default black, so none; spread 0.12
radians). It fades out as the sun sinks to 8 degrees below the horizon,
and shows only above the horizon.

`clouds` is a layer of cloud between `height` and `height + thickness`
metres over a round planet, lit by the first sun (or by the first moon
where there is no sun or it has set) and by the sky under it. `cover`
is the share of the sky it covers (0 to 1; from about 1.4 up the layer
is uniform), `density` the light it turns aside per metre where it is
thickest, and `span` how far across the largest clouds run. The clouds
veil the sky behind them and thin the light of every sun and moon that
reaches a point through them. A camera ray, and a ray that has passed
only exact surfaces since, marches through them in full; any other ray
reads them off a table laid out once. The table is the biased extra
`feature_biased_clouds`; built off (reference_architecture.md), every ray marches.

`camera.aperture` is the radius of the lens in metres; 0 (the default)
is a pinhole, sharp at every distance. `focus` is how far in front of
the eye the plane that stays sharp lies, measured along the view; 0
(the default) puts it through the target.

## The standard library

`scenes/standard.sdf` is plain scene code. It defines:
- shapes: `sphere`, `box`, `cylinder`, `capsule`, `cone` (height,
  base and tip radii, standing about its middle), `ellipsoid` (radii,
  the distance scaled by the smallest), `torus`, `plane`, `revolve` (a
  profile of [radius, height] points spun about z) and `sweep` (a round
  cone between two points);
- patterns: `checker` and `lean` (the sine of the angle between `n`
  and an axis, less `onset`, over `spread`).

## Formatting

The examples in this document are condensed. Scene files follow
these rules:

1. After `{` comes a new line, indented one tab; no block stays on one
   line, not even a one-statement `for` or `if`.
2. The same holds for `[` when the list's elements are lists or
   materials (profiles, boards, normals, colours): one element per
   line, with a trailing comma. A vector such as `[0, 0, 0.45]` stays
   inline.
3. An empty line follows each block, unless a closing `}` comes next.
   After an `if` or `for` block the empty line is optional. After a
   `with` or a join (`merge`, `cut`, `clip`) there is none, unless the
   block stands at the top level of the file.
4. In a run of consecutive `let`s or fields, the `=` line
   up. An empty line or a block starts a new run.

## Errors

The first error stops the load, which returns it as
`file:line:column: message` (rule 19).

## Camera tracks

`kodanuki scene.sdf --frames n --track cameras.txt --out directory`
draws a sequence of n frames, one sample a pixel each, and writes
`frame-0000.ppm` and `frame-0000.pfm` onward into the directory. The
track is a plain text file, not the scene language: one camera a line,
six numbers apart by spaces, the eye and then the target.

```
4.2 -5.1 3.3 0 0 0.9
4.1 -5.2 3.3 0 0 0.9
```

Frame k looks through line k; past the last line every frame looks
through the last one, so a track of one line holds the camera still.
The scene's `camera` gives every other field (`up`, `fov`, `aperture`,
`focus`). While the camera holds still, each frame continues the
pixels' estimates; once it moves, they start over. `--samples` sets
the batches and the light pass, not how many samples a frame draws, so
the last of n frames of a still track drawn with `--samples n --use
stopping=fixed` is the frame drawn with the same flags and `--out`
alone, to the byte. Any other line, and a track of no lines, is an
error.

`--reconstruction mean|temporal|temporal+denoise|temporal+denoise+upscale`
chooses how the frames are shown: `mean` (the default) is the running
mean above; `temporal` is the viewer's temporal accumulation;
`temporal+denoise` runs the denoiser after it;
`temporal+denoise+upscale`, the viewer's, then upscales the result to
the size. All but `mean` keep the guides while the camera moves, as the
viewer does. `--factor 1|2|3|4` (default 1) draws each frame at the
size divided by the factor, jittered, the phases in the Bayer order,
and writes it at the size; a factor above 1 needs
`temporal+denoise+upscale`, and a size that is a multiple of the factor.
The defaults draw the track as without the flags, to the byte.
