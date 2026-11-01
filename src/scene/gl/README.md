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
- `Textures` (`TextureCache`, `GpuTexture`): textures by material name,
  loaded once through the wallpaper's lookup layers: every mip of the
  first image, DXT kept compressed when the driver has S3TC, R8 and RG88
  as one and two channel textures, volume textures as 3D textures,
  filtering and wrapping from the texture flags, the usable region
  (image / allocated size). Sprite sheets upload every image and keep
  their frames (`spriteFrame()` picks the frame of a scene time); video
  textures play through `VideoTexture` (`updateVideos()` once per frame).
  Missing textures become a transparent 1x1 stand in, reported once.
- `VideoTexture` an MP4 read in place through mpv's stream callbacks played in a
  loop without sound into a texture through libmpv's render API; built
  only with libmpv (`CANVAS_SCENE_HAVE_MPV`), `available()` says so.
- `SceneRenderer` the renderer of `../render/README.md`: `load()` builds
  programs, textures, per layer passes, effect pass graphs and base
  passes; `advance(dt)` moves scene time, the pointer and parallax
  smoothing; `render(width, height)` runs the timeline animations, video
  textures, layout, camera shake and parallax, draws a frame once per
  `advance()` and presents it into the bound framebuffer by the scaling
  and clamp modes; `RenderDebug` holds the `--render-debug` switches.
  Particle layers (`../particles/README.md` sections 6 to 9) own a
  `ParticleSystem` with its child systems, stepped once per `advance()`
  while shown (whatever the number of frame sizes drawn) with the layer's
  factors, transform, control point overrides and the cursor in scene
  space (the output position through the fit bars and the view), and a
  `ParticleDraw` per particle file among them: the material's program
  with the renderer's combos (genericropeparticle for ropes), the sprite
  sheet layout (`SPRITESHEETBLENDNPOT` for padded sheets), trail values,
  orientation, and a vertex buffer growing by doubling: four corners per
  particle in genericparticle's layout, or per rope piece in
  genericropeparticle's (Catmull-Rom subdivisions for ropes, kept
  positions for rope trails). Every system draws with its own model
  matrix, parents first; `noParticles` (`--disable-particles`) leaves
  them out. Text layers (`../text/README.md` section 6) keep a
  `TextDraw`: the font (fonts load once per name: the wallpaper's lookup,
  fontconfig for `systemfont_` names, the assets' fallback), the layout,
  an R8 coverage texture rasterized at the layer's scale and the largest
  frame's pixels per unit, and an RGBA image the font material paints
  (straight alpha, or over the opaque background); each is redone only
  when its inputs change. The image is the layer texture of the image
  layer path: flattexture draws it, effects run on it through the base
  pass. Scenes with scripts (`../script/README.md`) get a `ScriptEngine`
  after their layers, with the renderer as its `ScriptHost` (layer sizes,
  text measured on demand; particle play, pause and stop: a stopped
  layer's system starts over when played); the scripts run once per
  `advance()` at the first frame drawn, after the timeline animations and
  before the layout, with the time, the output size, the cursor in scene
  space, the left button (`setPointerDown()`) and the topmost shown image
  or text layer under the cursor. Live property changes reach them
  (`setProperty()`), their failures become problems and their console
  lines go to the log; `Options::scriptStorage` names the localStorage
  file. GL objects need the context current, including at
  destruction.

Tests: `claude_tests/test_scene_gl` and `test_scene_render` (opt-in GL
parts; llvmpipe, every device with `CANVAS_GPU_ALL=1`); the script runtime
in `test_scene_script`.
