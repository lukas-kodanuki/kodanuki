# Changing the renderer's code

Read `reference_rules.md` in full and `reference_architecture.md`;
`reference_geometry.md` too for the tree and the walk.

- The core is `source/<stage>/`, the extras `source/extras/` (rule 35).
  Adding or changing an extra: `task_extra.md`.
- Headers are documented, sources, tests and tools have no comments
  (rules 36, 43).
- Format every file you change:
  `clang-format -style=file:target/.clang-format -i <files>`, and
  remove empty lines inside functions by hand (rules 13, 38).
- Build one target at a time, then run the checks:

  ```
  ninja -C output/build -j1 kodanuki_tests   # after make test configured it
  make test
  ```

  `make test CONFIG=target/configs/render_final.toml` checks the build
  with the biased extras off; both must pass.
- `target/tools/lines.sh` counts the code lines, core and extras apart.
- `target/tools/compare.py` compares two frames.
