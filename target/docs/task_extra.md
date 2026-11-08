# Adding or changing an extra

Read `task_code.md` first, then the extension points in
`reference_architecture.md`.

1. **Folder.** `source/extras/<point>_<category>_<name>/`: its main
   extension point, then `correct`, `consistent`, `biased` or `mixed`,
   then its name (rule 35). It depends on the core only.
2. **Plug in.** Add a member to the point's `Member` enum and a case to
   its one dispatch function; the extra's typed state is a field of
   `Prepared`. The core includes the extra only in that dispatch.
3. **Switch.** Each extra is `true` or `false` in a config's `[extras]`,
   and `target/workflow.py` must know how to turn it off:
   - a `--use` member (`useKeys` and `readUse` in
     `source/frame/run.cpp`): add it to `JOINED` (one of several
     members joined by `+`) or `CHOSEN` (one value or the other);
   - else a build switch: `#ifdef KODANUKI_OFF_<FOLDER>` around a
     `constexpr bool` in its header, and the name in `BUILT`;
   - else neither: the name in `ALWAYS`, and `false` is refused.

   The workflow refuses to run while a folder is missing from these
   tables.
4. **Configs.** Add the extra to every file in `target/configs/` at the
   default the source gives it; `render_final.toml` leaves out biased
   and mixed extras.
5. **Tests.** Extend the point's generic test (`reference_architecture.md`).
   A check that needs the extra built reads its constant, as
   `check(!faceSlabsBuilt || ...)`, so that `make test` passes with it
   built off too.
6. **Docs.** Name it in the point's row of the extension point table,
   and in `reference_language.md` if a scene can see it.
