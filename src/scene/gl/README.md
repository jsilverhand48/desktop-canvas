# src/scene/gl/

The scene engine's GL code, library `canvas_scene_gl` (GL 3.3 core through
libepoxy, links `canvas_scene_core`). Everything here needs a current GL
context; the GPU free parts live in the other `src/scene/` modules.

## Files

- `ProgramCache` the program cache of `../shader/README.md` section 8:
  `request(vertex, fragment)` returns a handle (one per source pair);
  programs come from memory, from disk (`DCPB0001` files holding the
  driver string, both sources and the program binary, used only on an
  exact match and when the driver accepts the binary) or from the driver,
  with `GL_KHR_parallel_shader_compile` making `ready()` non blocking.
  Failed programs carry the stage and driver log. `fnv1a64` names the
  files.

Rendering (layers, effects, compositing) arrives with phase P4.

Tests: `claude_tests/test_scene_gl` (opt-in; llvmpipe, every device with
`CANVAS_GPU_ALL=1`).
