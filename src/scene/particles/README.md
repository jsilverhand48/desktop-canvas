# src/scene/particles/

Specification of Wallpaper Engine particle systems: the particle file,
the simulation (emitters, initializers, operators), control points,
children, and how particles are drawn. Phase P5a covers sections 1 to 7
except what is marked P5b; phase P5b adds control points (section 8),
children (section 9), the trail renderers and renderer orientations.
Evidence tags as in `../README.md`. Corpus numbers come from
`claude_tests/corpus/particle_survey.py` over the 207 scenes of
2026-10-31: 145 scenes name particle systems, 619 particle files with
their children; [asset: presets] are the 813 files under the assets
folder's `presets/`, Wallpaper Engine's own particle presets.

Particle code is deterministic: each system draws from its own random
generator, seeded from the object id and file path, and time comes from
the scene clock (`../render/README.md` section 8), so a scene renders the
same frames every run.

## 1. The particle file

A scene object with `"particle": path` names a JSON file (`particles/
*.json` in the package or the assets folder) [corpus 619/619]:

| Key | Meaning |
|---|---|
| `material` | material file; its pass uses shader `genericparticle` [corpus 619/619], blending `additive` 515, `translucent` 103, `alphatocoverage` 1; combos `REFRACT`, `CUTOUT` |
| `maxcount` | most live particles [doc: "Controls the maximum number of particles allowed to be spawned"] (20, 500, 16, 50, 8 most often [corpus]); this project also caps it at 100 000 |
| `starttime` | seconds simulated when the system is created [doc: "Will pre-simulate the particle system for a certain amount of time"] |
| `flags` | system flags (below) |
| `animationmode` | `sequence` 35 or `randomframe` 79 [corpus]: "either spawns a random frame of your sprite sheet or plays the sprite sheet in sequence" [doc: General] |
| `sequencemultiplier` | "the speed at which the sprite sheet sequence is played" [doc] |
| `emitter`, `initializer`, `operator`, `renderer` | lists of components `{name, id, ...}` (sections 3 to 6) |
| `controlpoint` | up to 8 control points (section 8) |
| `children` | child systems (section 9) |

System flags (no source names them; read from the presets and the
corpus) [inference]:
- 1 worldspace ("particles will ignore the position and rotation of the
  particle system" once created [doc: General]): set on the presets'
  firework explosions and firefly trails [asset: presets], and on 46 of
  the 48 corpus systems that have it, trail renderers [corpus];
- 2 random sprite sheet frame (the older form of `animationmode`
  `randomframe`): 47% of its systems use sprite sheets and none plays a
  sequence [corpus]; torchembers sets both [asset: presets];
- 4 no frame blending ("Disables the smooth blending between frames"
  [doc]): 67% of its systems are sprite sheets [corpus];
- 8, 16, 32, 64, 128 disable the color, count, lifetime, size and speed
  overrides of the layer [doc: General lists them in this order]: one
  corpus system sets all five (248), and the presets' refraction
  firework sets 8 (a colour override would break its distortion)
  [asset: presets].

## 2. Simulation

Each particle has a position, velocity, rotation (x, y, z radians),
angular velocity, size, colour (rgb 0..1), alpha, age, lifetime, and the
values its initializers gave size, colour and alpha (operators scale
those) [doc: "Operators then change those values over time, using the
Initializer values as a starting point", Particle Systems Overview].
Positions are in the system's space (the object's, after parents), or in
scene space for worldspace systems.

One step of dt seconds, multiplied first by the layer's `rate` factor
("Factor to control simulation rate" [doc: SceneScript
IParticleSystemInstance]): emitters spawn (section 3), initializers set
the new particles (section 4), operators update every live particle
(section 5), ages advance, and particles at or past their lifetime die.
`starttime` runs whole steps of 1/30 s before the first frame
[inference: the step]. A hidden system (its object or an ancestor not
visible [req A4, A9]) neither simulates nor draws, and carries on where
it stopped once shown [inference].

Layer overrides (scene.json `instanceoverride`) are factors, 1 leaving
the system unchanged [doc: IParticleSystemInstance]: `count` "controls
emission rate", `speed` "initial velocity and forces", `size`,
`lifetime`, `alpha`, `colorn` (rgb 0..1, "the color property is spelled
colorn ... for backward compatibility"); scene files also hold `color`
(rgb 0..255, the older form, 43) and `brightness` (4), which multiplies
the colour [inference]. Overrides seen: colorn 334, rate 308, size 228,
alpha 228, speed 216, count 152, lifetime 84 [corpus]. A system flag
8..128 ignores the matching override (section 1).

## 3. Emitters

`sphererandom` 499 and `boxrandom` 120 [corpus], each:
- `rate` particles per second ("The number of particles spawned per
  second" [doc: Emitters]) times the `count` override, accumulated over
  steps; `instantaneous` particles at once when the system starts
  ("If Rate is 0, the system spawns those particles once" [doc]);
- `duration` seconds of emission, 0 forever; `delay` before the first
  [doc]; random periodic emission (emitter flag 4 [corpus: the 8
  emitters with periodic fields set it]): emit for a random time between
  `minperiodicduration` and `maxperiodicduration`, pause for one between
  `minperiodicdelay` and `maxperiodicdelay`, at most
  `maxtoemitperperiod` particles per period (0: no limit) [doc];
  emitter flag 2 is "Limit to one per frame" [doc; corpus and presets:
  28 of its 29 emitters feed rope renderers, which the docs pair with
  it];
- spawn position: `origin` offset plus a random point. Sphere: a random
  direction, uniform over the sphere, at a distance uniform between
  `distancemin` and `distancemax`, scaled per axis by `directions`
  ("A value of 1 equals Distance Max", 0 removes that axis [doc]), and
  each axis's sign forced by `sign` (0 both, 1 positive, -1 negative
  [doc]). Box: each axis uniform between -`distancemax` and
  `distancemax` (a vector [corpus 114/120]) times `directions`
  [inference];
- `speedmin`, `speedmax`: an initial speed away from the system centre
  along the spawn direction [doc: "The minimum particle speed in
  conjunction with a movement Operator"; inference: the direction];
- `controlpoint`: emit around that control point (P5b);
- `audioprocessing*`: "The emitter is active only while audio plays"
  [doc]: silent until phase P7 brings audio.

## 4. Initializers

Random values follow `min + (max - min) * r^exponent` with r uniform in
0..1: exponent 1 is "a balance between both", towards 0 biases to max,
2 and higher to min [doc: Initializers "Exponent"]. Vectors draw one r
per component; colours one r for all components, since min and max are
"the primary color" and "the secondary color" [doc; inference].

| Name | [corpus] | Sets |
|---|---|---|
| `lifetimerandom` | 619 | lifetime in seconds, times the `lifetime` override |
| `sizerandom` | 618 | size (the sprite's width in scene units), times `size` |
| `colorrandom` | 550 | colour, `min`/`max` rgb 0..255, times `colorn` |
| `velocityrandom` | 291 | velocity per axis, times `speed` (needs a movement operator [doc]) |
| `rotationrandom` | 262 | rotation in radians, a vector or a number for z [corpus: values like 6.283] |
| `alpharandom` | 176 | alpha, times `alpha` |
| `turbulentvelocityrandom` | 168 | a velocity of random speed (`speedmin`..`speedmax`) along `forward` (default up, 0 1 0) turned by smooth noise at the spawn point (`scale`, `timescale` the noise speed, `offset` "a directional offset to the Forward value", phase between 0 and `phasemax`) [doc; inference: the noise] |
| `angularvelocityrandom` | 105 | angular velocity per axis in radians per second |
| `positionoffsetrandom` | 2 | moves the spawn point by noise of `distance`, `scale`, `timescale` [doc; inference] |
| `mapsequencebetweencontrolpoints`, `mapsequencearoundcontrolpoint`, `remapinitialvalue` | 8, 3, 1 | phase P5b |

Initializers outside the corpus (`hsvcolorrandom`, `colorlist`,
`inheritcontrolpointvelocity`, inherit value from event) are reported
and skipped.

## 5. Operators

Operators may fade their effect in and out over a particle's life:
`blendinstart`..`blendinend` ramps from 0 to full, `blendoutstart`..
`blendoutend` back to 0, as fractions of the lifetime [doc: "Operator
blending"]; linear ramps [inference]. f below is age / lifetime.

| Name | [corpus] | Effect |
|---|---|---|
| `movement` | 578 | velocity += `gravity` * dt; velocity *= exp(-`drag` * dt) (negative drag accelerates [doc]); position += velocity * dt. Forces scale with the `speed` override [doc]. Flag 1: gravity in scene space for systems that are not worldspace (the "Worldspace" option [doc]) |
| `angularmovement` | 106 | angular velocity += `force` * dt, *= exp(-`drag` * dt); rotation += angular velocity * dt |
| `alphafade` | 594 | alpha *= f / `fadeintime` before the fade in time, (1 - f) / (1 - `fadeouttime`) after the fade out time [doc: "The time percentage until the fade-in is completed", "at which the fade-out starts"]; both default to 0.5 [inference: 332 of 576 give only `fadeintime`, among them Wallpaper Engine's own example.json, and their particles must still fade out] |
| `sizechange` | 176 | size *= lerp(`startvalue` 1, `endvalue` 0, (f - `starttime` 0) / (`endtime` 1 - `starttime`)) clamped [doc; inference: the defaults, e.g. fireworks3 gives only `endvalue` 0.05] |
| `alphachange` | 15 | alpha *= the same ramp of `startvalue`, `endvalue` (defaults 1, 0) |
| `colorchange` | 62 | colour *= the same ramp between rgb 0..1 `startvalue` and `endvalue` (default white) [corpus: values in 0..1] |
| `oscillatealpha` | 90 | alpha *= lerp(`scalemin`, `scalemax`, 0.5 + 0.5 sin(phase + 2π freq f)), freq per particle between `frequencymin` and `frequencymax` "oscillations per particle lifetime", phase between `phasemin` and `phasemax` [doc; inference: the sine] |
| `oscillatesize` | 9 | size *= the same wave between `scalemin` and `scalemax` [doc] |
| `oscillateposition` | 52 | position += `mask` (default 1 1 0) * the wave's (−1..1) value * scale, scale between `scalemin` and `scalemax` in scene units [doc; inference: the units] |
| `turbulence` | 76 | velocity += smooth noise of (position * `scale`, time * `timescale`) * `mask` * a per particle speed between `speedmin` and `speedmax` * dt [doc; inference: the noise] |
| `capvelocity` | 1 | limits the speed to `maxspeed` [doc] |
| `controlpointattract`, `vortex`, `vortex_v2`, `remapvalue`, `reducemovementnearcontrolpoint`, `maintaindistancetocontrolpoint`, `maintaindistancebetweencontrolpoints` | 73, 4, 1, 7, 4, 1, 2 | phase P5b |

Noise is this project's: smooth 3D value noise (deterministic, seeded per
system) [inference: no source gives Wallpaper Engine's].

## 6. Rendering

Renderers [corpus]: `sprite` 476 (P5a), `spritetrail` 75, `ropetrail`
30, `rope` 20 (P5b). Orientation `screen` is the default: "particles
always face the user's perspective" [doc: Renderers]; `upright` and
`fixed` (with `axis`) are P5b.

A sprite is a quad drawn with Wallpaper Engine's `genericparticle`
shader in its path without geometry shaders (`GS_ENABLED` 0) [asset:
genericparticle.vert]: four vertices per particle, each with
`a_Position` the particle position, `a_TexCoordVec4` (corner u, corner
v, rotation z, size), `a_TexCoordC2` (rotation x, y), `a_Color` (rgb,
alpha) and, when `THICKFORMAT` is set (sprite sheets and trails),
`a_TexCoordVec4C1` (velocity, frame value). The shader places the corner
at position + size * right * (u - 0.5) - size * up * (v - 0.5) * texture
ratio [asset: common_particles.h], so texture v 0 is the sprite's top,
as everywhere (`../render/README.md` section 1).

Engine values [asset: common_particles.h; doc: Variables]:
`g_OrientationRight`, `Up`, `Forward` (1 0 0, 0 1 0, 0 0 1 for screen
orientation in these orthographic scenes), `g_ViewRight`, `g_ViewUp`,
`g_EyePosition`, `g_ModelMatrix` and its inverse,
`g_ModelViewProjectionMatrix` (the view projection alone for worldspace
systems), `g_Texture0Resolution`, and for sprite sheets `g_RenderVar1` =
(frame width, frame height as fractions of the texture, frame count,
frame height / frame width), read from the texture's frames (the first
frame's size; frames fill rows left to right [asset: ComputeSpriteFrame
steps frames that way]). The frame value is age / lifetime *
`sequencemultiplier` (default 1) in sequence mode, a fixed random 0..1
per particle in random frame mode; `SPRITESHEET` and, unless frames do
not blend, `SPRITESHEETBLEND` are set [asset: genericparticle.frag].

Blending follows the material: `additive` adds src to dst
(`../render/README.md` section 4), `translucent` as there,
`alphatocoverage` drawn translucent [inference]. A particle layer draws
in `objects` order like any layer; its `g_Overbright` and other material
constants work as section 3 of `../render/README.md` describes.
`desktop-canvas-scene --disable-particles` (and `--render-debug
no-particles`) leaves particles out.

## 7. Capability

The report (`../model/README.md` section 9) names `particles` for any
particle layer and adds `particle-controlpoints`, `particle-children` and
`particle-trails` when systems use what phase P5b brings, so the "auto"
engine keeps linux-wallpaperengine for those until it does.

## 8. Control points (P5b)

Each system has 8 control points, `0` always "tied to the origin
position of the particle system" [doc: Control Points]. Entries `{id,
flags, offset, locktopointer, parentcontrolpoint, angles}` [corpus 4731
entries]: `offset` "the (relative) position"; flag 1 (or
`locktopointer` true) locks the point "to the mouse cursor" [doc;
asset: the presets' cursor follow and avoid examples set flag 1 on the
point they use]; flag 2 "Worldspace": the offset is a fixed scene
position [doc; inference: 11 of its 15 entries move the offset off 0];
flag 4 "Copy from parent" with `parentcontrolpoint` [doc; corpus 8/8].
Layer overrides `controlpoint<n>` (vec3) and `controlpointangle<n>` set
point n [doc: IParticleSystemInstance].

## 9. Children (P5b)

`children` entries `{id, name (a particle file), type, origin, angles,
scale, maxcount, probability, controlpointstartindex}` [corpus 155]:
`static` (also when `type` is absent, 106 [inference]) spawns once at
the system origin; `eventfollow` follows each particle; `eventspawn`
starts where a particle is born; `eventdeath` where one dies [doc:
Children].

## Files

(added with the implementation)
