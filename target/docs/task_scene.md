# Writing and drawing a scene

Read `reference_language.md`; its last section is the layout scene files
follow. `scenes/standard.sdf` holds the shared shapes and patterns, and
the other files in `scenes/` are examples.

Draw a scene with a config from `target/configs/`:

```
make render SCENE=scenes/city.sdf CONFIG=target/configs/render_fast.toml
make render SCENE=scenes/city.sdf CONFIG=target/configs/render_final.toml
make viewer SCENE=scenes/city.sdf CONFIG=target/configs/viewer_fast.toml
```

- `render_fast` iterates quickly with every extra; `render_final` draws
  the final frame without the biased extras; `viewer_fast` flies through
  the scene, and R in the viewer loads the file again.
- Frames land in `output/<scene>.ppm` (graded) and `.pfm` (linear).
- The settings a config takes are in rule 51 of `reference_rules.md`.
- While trying things, draw small: a config of your own with a lower
  `width` and `height` and a higher `tolerance`.
