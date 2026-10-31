# src/scene/

The native Wallpaper Engine scene engine (namespace `canvas::scene`),
written from specs as an original implementation. It is hosted by
desktop-canvas-scene (`src/scene-host/`) through the `Renderer` interface.

## Clean-room rules

linux-wallpaperengine (lwe) is GPL-3.0 and this project is MIT: nothing of
lwe's code may enter here. Each module's README.md is its specification and
is written before the module's code, from these sources only:

- **[doc]** the official Wallpaper Engine documentation
  (docs.wallpaperengine.io), cited by page;
- **[asset]** files of the installed Wallpaper Engine assets folder
  (shaders, materials, effects, `.tex-json` import settings, ...), cited by
  path;
- **[corpus]** measurements over the user's workshop library, produced by
  throwaway scripts that test a written hypothesis against every file, cited
  with their result ("203/203 packages");
- **[bb]** lwe used as a black box through
  `claude_tests/blackbox/lwe-run.sh` (its screenshots, logs and `--help`).

Statements without a tag are design decisions of this project, not claims
about the format. "Known from memory" is not a source. lwe's source, docs
and build tree, and any other Wallpaper Engine renderer or extractor, are
never opened (enforced for agent sessions by the hook in
`.claude/hooks/cleanroom-guard.py`). The user's own lwe patches were
converted once into behavior requirements, recorded in the plan's
Appendix A; they are cited as **[req A<n>]**.

## Modules

| Directory | Contents | Phase |
|---|---|---|
| `assets/` | scene.pkg archives, file lookup across item, package, Wallpaper Engine compatibility overrides and assets, `.tex` textures (implemented) | P1 |
| `model/` | project, scene, object, material, effect and model JSON, user properties, capability report (implemented) | P2 |
| `shader/` | Wallpaper Engine shader dialect, combos, translation to GLSL 3.30 (implemented; HLSL conversion rules pending, see its section 7) | P3 |
| `gl/` | GL 3.3 code: program cache (implemented), rendering from P4 | P3, P4 |
| `render/` | GL 3.3 core compositing, effects, camera, bloom | P4 |
| `particles/` | particle systems | P5 |
| `script/`, `text/` | SceneScript (QuickJS-ng), text layers | P6 |
| `audio/` | sound layers, spectrum | P7 |
| `puppet/` | puppet warp meshes, skeletons, animation | P8 |

## Libraries

`canvas_scene_core` holds everything that needs no GPU (parsing, decoding,
simulation) and is deterministic, so tests run it headless. Today it is the
`assets/` readers and the `model/`; it links lz4, libjpeg-turbo and libpng
privately and canvas_core and `canvas_scene_props` publicly.
`canvas_scene_props` is `model/UserProperties` alone, so the daemon shares
it without linking the engine. `canvas_scene_gl` is `gl/` (libepoxy). GL code will live in
`canvas_scene_gl` from P4 on.

Verification of the readers: `claude_tests/test_scene_assets` (fixtures
built in code), the opt-in `test_scene_corpus` (every texture of a real
library: 203 scenes, 1475 textures, 4668 mips, 0 failures, peak 385 MiB
on 2026-10-30), `test_scene_dxt_gl` (CPU DXT decoding against the GL
drivers: within 1 on NVIDIA and llvmpipe, 2 on radeonsi) and libFuzzer
targets (`DESKTOP_CANVAS_FUZZ`). The model: `test_scene_model`, the corpus
test loading all 203 scene models (0 file errors, 0 warnings) and a model
fuzz target.
