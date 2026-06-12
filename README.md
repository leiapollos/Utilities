# Utilities

A personal engine for games and apps: a thin platform host that
hot-reloads a product module. macOS/Metal and Windows/Vulkan.

## Layout

```
nstl/      base library (arenas, strings, os, gfx, ui, audio, jobs...)
engine/    the framework: frame loop, 2D/3D renderers, assets, sim/replay
projects/  products; each is one module built against the engine
host/      platform shell; loads and hot-swaps the module over a tiny ABI
cooker/    asset compiler (.glb -> umdl/utex, .wav -> uaud)
meta/      metagen: table -> codegen (shader ABI records)
tests/     headless test runner
sob.c      the build system (self-rebuilding single C file)
```

The dependency rule: arrows point one way.

- `nstl` knows nothing above it.
- `engine` includes `nstl`, never project files. The single seam is
  `eng_project_()`: every project defines it and hands the engine one
  table.
- `projects` include the engine; the active project's `<name>_main.cpp`
  is the whole translation unit.
- `host` knows only `engine/engine_interface.hpp` (the ABI), not the
  engine.

Where new code goes: it starts in the project that needs it. It moves
into `engine/` when a **second** project needs it, and into `nstl/`
when it's engine-agnostic. Nothing is promoted on speculation.

## A window in five minutes

```
cp -r projects/hello projects/myapp
cd projects/myapp && for f in hello*; do mv "$f" "${f/hello/myapp}"; done
# in myapp.cpp: rename hello_* symbols, set .name and the state id
./sob run myapp
```

That's a hot-reloading window with a panel and the debug overlay. Edit
`myapp.cpp` while it runs — the host rebuilds and swaps the module;
a broken edit keeps the old module alive until the next good build.

`projects/hello/` is the reference: ~90 lines for a window, a text
edit, a button, and a stat line. The `EngProject` table uses designated
initializers — declare only what you use:

```c
static const EngProject* eng_project_(void) {
    static const EngProject project = {
        .name = "myapp",
        .stateId = ENG_STATE_ID('M', 'Y', 'A', 'P'),
        .stateVersion = 1u,
        .stateSize = sizeof(MyState),
        .stateAlignment = alignof(MyState),
        .state_init = my_state_init_,
        .panels = my_panels_,
    };
    return &project;
}
```

Capabilities are opt-in; off means you don't pay for it:

- `ENG_CAP_WORLD3D` — the GPU-driven 3D world renderer (cull/draw
  pipelines, model/texture assets)
- `ENG_CAP_SIM` — fixed-tick sim (60 Hz) with save files, input
  recording, and checksum-verified replay
- `ENG_CAP_AUDIO` — cooked audio publishing into the host mixer

Assets: drop sources into `projects/myapp/assets/src/` (`.glb`,
`.wav`), add one row per asset to the project's X-list (see
`projects/demo/demo_assets.h`), run `./sob cook myapp`. Enums, paths,
and labels derive from the list; the cooker discovers sources by
scanning the directory.

## Build

```
./sob [target] [mode] [project]
```

| target  | does                                              |
|---------|---------------------------------------------------|
| run     | build host + module, launch (default target)      |
| dev     | build host + module in parallel                   |
| module  | build the hot-reloadable project module           |
| host    | build the platform host                           |
| ship    | single static executable, no hot reload           |
| test    | build + run the headless test suite               |
| cook    | cook `projects/<p>/assets/src` (mtime-skipped)    |
| shaders | compile slang -> metal/spirv into `build/shaders` |
| metagen | regenerate code from `.metadef` tables            |
| clean   | delete build output                               |

Modes: `debug` (default), `asan`, `release`. Project defaults to
`demo`.

## Debug keys

- `Esc` quit, `F1` stats overlay, `F2` profiler
- with `ENG_CAP_SIM`: `F5` save, `F9` load, `F6` record, `F7` replay

## Notes

- Hot reload is manifest-driven: each module build dep-scans the TU
  into `build/module_inputs.txt`; the host polls exactly those files.

---

Inspired by [Handmade Hero](https://handmadehero.org/) and the people
who build engines this way.
