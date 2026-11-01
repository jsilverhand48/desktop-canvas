# src/scene/particles/

Specification of Wallpaper Engine particle systems: the particle file,
the simulation (emitters, initializers, operators), control points,
children, and how particles are drawn. Phase P5a covers sections 1 to 7
except what is marked P5b; phase P5b adds control points (section 8),
children (section 9), the control point initializers and operators
(sections 4 and 5), the trail renderers and renderer orientations
(section 6). Evidence tags as in `../README.md`. Corpus numbers come from
`claude_tests/corpus/particle_survey.py` over the 207 scenes of
2026-10-31: 145 scenes name particle systems, 619 particle files with
their children; [asset: presets] are the 813 files under the assets
folder's `presets/`, Wallpaper Engine's own particle presets. The P5b
numbers come from `claude_tests/corpus/p5b_survey.py` (values per key of
the P5b components in the workshop files and in the presets).

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
Without an initializer for it a particle is white, opaque, 20 units
wide and lives 1 s [inference: Wallpaper Engine's example.json and
every corpus system set lifetime and size].
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
8..128 ignores the matching override (section 1). The factors scale
every new particle's lifetime, size, alpha and colour, whether an
initializer set them or not [inference: a `colorn` override must tint a
system without `colorrandom`], and `speed` its initial velocity.

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
  `maxtoemitperperiod` particles per period (0: no limit) [doc]; a step
  counts in the phase it starts in, leftover time carrying into the next
  phase [inference];
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
- `controlpoint` (default 0): the spawn centre is that control point
  (section 8) plus `origin` ("Sphere random and box random spawn
  particles based on a control point's position" [doc: Control Points]);
  point 0 is the system origin unless it follows the pointer, which 7
  corpus systems do to spawn at the cursor, like the presets' mouse rope
  [asset: presets interactive/trail_1];
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
| `mapsequencebetweencontrolpoints` | 8 | (P5b) moves new particles onto the line from control point `controlpointstart` (0) to `controlpointend` (1) [doc: Position between control points]: the particle takes the next sequence value u (below) of `count` (default 10 [inference]) steps and its place along the line p = `bounds` x + (y - x) u (bounds "0 1"); its position becomes the line point there plus its offset from the emitter's centre. Flags [inference: the presets' discharge and dischargearc differ only in flag 8 and the arc; the doc lists the options in this order]: 1 scales that offset by sin(π p) ("reduce outer positions"), 2 the velocity ("reduce outer velocity"), 4 the size ("reduce outer size"), 8 bends the line into an arc of height `arcamount` (default 0.1 [inference]) times the line length at the middle, 4 p (1 - p) along the line's left normal in the xy plane |
| `mapsequencearoundcontrolpoint` | 3 | (P5b) moves new particles onto a circle about `axis` (default 0 0 1) through control point `controlpoint` (0) [doc: Position around control point]: angle 2π (`bounds` x + (y - x) u) from the plane's first axis (x for the default axis), radius the length of the particle's offset from the emitter's centre across the axis ("bind your emitter to the same control point, otherwise the size of the circle will change" [doc]); velocity gains a random vector between `speedmin` and `speedmax` read as (along the circle, outwards, along the axis) [inference: corpus and presets give only the second component, a growing ring] |
| `remapinitialvalue` | 1 | (P5b) at creation [doc: "the changes are applied at the time of the particle creation"]: the `input` (`distancetocontrolpoint` to `inputcontrolpoint0` [corpus 1/1]; other inputs read 0) mapped from [`inputrangemin` 0, `inputrangemax` 1] to [`outputrangemin` 0, `outputrangemax` 1] goes to `output` (default `size` [inference: the doc's example output]) by `operation` like `remapvalue` (section 5) |

Sequence values [inference: doc "Repeat: the particles will spawn in a
full loop", "Mirror: the particle spawn point will move back and forth"]:
the n-th particle a component places (n from 0) has s = n / count;
`limitbehavior` `repeat` (default) gives u = frac(s), `mirror` the
triangle wave u = 1 - |1 - 2 frac(s / 2)|. Fractional counts (2.2, 3.02
[corpus; asset: presets dna]) make the places precess.

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
| `controlpointattract` | 73 | (P5b) within `threshold` (default 512 [inference: the presets' interactive trail]) of control point `controlpoint` (0) moved by `origin`: velocity += the unit vector towards it * `scale` * the speed factor * dt; positive pulls, negative pushes [doc: Control Point Force], with the same strength over the whole distance [inference]. 56 of the 73 act on a point locked to the pointer, so particles follow or avoid the cursor [corpus]. Its flags 1 (reduce velocity near the centre) and 2 (delete particles in the centre) appear in no corpus file and are not done [doc; corpus: flags 0 only] |
| `vortex` | 4 | (P5b) turns particles about `axis` (default 0 0 1) through control point `controlpoint` (0) at a speed along the circle of `speedinner` within `distanceinner` of it, `speedouter` from `distanceouter` on, linear between (scene units per second, positive counterclockwise looking against the axis) [doc: Vortex]; flag 1 measures the distance from the axis ("Infinite axis: the influence range becomes cylinder-shaped instead of a sphere" [doc]), otherwise from the point. The turn moves positions and keeps the radius [inference: the presets' dna sets both distances 0 and speed -250, a helix that turns with no other force holding its radius]. With an audio response (`audioprocessingmode` not 0) the vortex stands still without audio ("ties the particle speed to audio playback, causing the vortex to stop spinning" [doc]) until phase P7 |
| `vortex_v2` | 1 | (P5b) `vortex`, and with flag 2 the ring shape [doc: Vortex; asset: presets magic_vortex_orb]: distances count from the ring of radius `ringradius` (default 256 [inference]) and width `ringwidth` (default 0) about the axis; particles farther than `ringpulldistance` (default 0) from the ring are "unaffected by any forces" [doc], those within it are pulled towards the ring at `ringpullforce` (default 1000 [inference]) units/s² |
| `remapvalue` | 7 | (P5b) every step ("continuously applied to all particles" [doc: Remap Value]): x is the `input` (default the particle's life fraction [inference]; `particlesystemtime` the system's time [corpus 1/7]; `distancetocontrolpoint` to `inputcontrolpoint0`) mapped from [`inputrangemin` 0, `inputrangemax` 1] to 0..1 (clamped with flag 1 [doc: "Clamp input value"; inference: the bit]); a `transformfunction` (`sine`, `simplexnoise`, `fbmnoise` [corpus]) replaces x by 0.5 + 0.5 f(x * `transforminputscale` (1) + a phase per particle) with this project's noise (fbm: three octaves) [inference]; y = lerp(`outputrangemin` 0, `outputrangemax` 1, x) (clamped to that range with flag 2 [doc]) goes to `output` by `operation`: `remap` (corpus 4/4) and `assign` set it, `multiply`, `add` and `subtract` [doc] combine. Outputs: `velocity` (a vector range), `speed` (the velocity's length, direction kept), `opacity` and `size` multiply the particle's values after the other operators [corpus: velocity 4, speed 2, opacity 1]; the default operation is `remap` [inference] |
| `reducemovementnearcontrolpoint` | 4 | (P5b) within `distanceouter` of control point `controlpoint` (0): velocity *= exp(-k dt), k = `reductioninner` within `distanceinner`, then linearly to `reductionouter` (0) at the outer distance [doc: "reduces the movement speed of particles in its range. Negative reduction values result in a speeding up"; inference: the drag form] |
| `maintaindistancetocontrolpoint` | 1 | (P5b) moves particles towards the sphere of radius `distance` (default 256 [inference: the presets' orb pairs it with a 256 ring and gives no distance]) about control point `controlpoint` (0) by 1 - exp(-`variablestrength` (1) dt) of the gap per step [doc: "places particles in a circle around a provided control point", "Factor on how strong the position should be enforced"; inference: the form] |
| `maintaindistancebetweencontrolpoints` | 2 | (P5b) keeps each particle at its place along the line from `controlpointstart` (0) to `controlpointend` (1) while the points move: it moves by the change of the line point at its place p (from `mapsequencebetweencontrolpoints`, else its projection on the line at birth) [doc: "distributes particles between two control points"; inference] |

Noise is this project's: smooth 3D value noise (deterministic, seeded per
system) [inference: no source gives Wallpaper Engine's].

## 6. Rendering

Renderers [corpus]: `sprite` 476 (P5a), `spritetrail` 75, `ropetrail`
30, `rope` 20 (P5b, below). Orientation `screen` is the default:
"particles always face the user's perspective" [doc: Renderers];
`upright` "orients the particles upright towards the screen", the same
as `screen` for these orthographic views [inference]; `fixed` aligns
sprites with `axis` [doc]: forward = `axis`, right = up (0 1 0) x
forward (x 1 0 0 when parallel), up = forward x right [inference]
(corpus: 1 upright trail, no fixed).

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

The albedo's format code becomes the `TEX0FORMAT` define, so one and
two channel textures (R8, RG88) read as white with alpha and as grey
with alpha [asset: `genericparticle.frag` and `genericropeparticle.frag`
call `ConvertTexture0Format`, which tests `TEX0FORMAT`, while no sampler
annotation sets it; inference: the engine defines it for particle
materials]: refraction systems in 1327266146 otherwise draw as opaque
red and green squares.

Sprite sheets whose image is smaller than their texture (5 corpus
systems) also get `SPRITESHEETBLENDNPOT`, which lays the frames out in
the image's part of the texture (`g_Texture0Resolution.z / .x`) [asset:
common_particles.h ComputeSpriteFrame; inference: when it is set].

Blending follows the material: `additive` adds src weighted by its
alpha to dst (`../render/README.md` section 4), `translucent` as there,
`alphatocoverage` drawn translucent [inference]. A particle layer draws
in `objects` order like any layer, its children after it (section 9);
its `g_Overbright` and other material constants work as section 3 of
`../render/README.md` describes. `desktop-canvas-scene
--disable-particles` (and `--render-debug no-particles`) leaves
particles out.

### 6.1 Trails and ropes (P5b)

Sprite trails (`spritetrail` [corpus 75]) draw as sprites with
`TRAILRENDERER` and `THICKFORMAT` set: the shader turns each quad along
the particle's velocity and stretches it to size * max(`minlength`,
min(|v| * `length`, `maxlength`)) [asset: common_particles.h
ComputeParticleTrailTangents reads g_RenderVar0 = (length, maxlength,
minlength)]; defaults length 1, maxlength 10, minlength 0 [inference:
49 corpus trails give no maxlength and 71 no minlength; the presets set
them where streaks must stay short]. A particle without speed draws
nothing (the shader divides by the speed) [asset].

Ropes (`rope` [corpus 20]) draw with Wallpaper Engine's
`genericropeparticle` shader [asset] in place of the material's
genericparticle (every particle material names genericparticle [corpus
619/619; inference: the engine swaps the shader]) with `THICKFORMAT` set:
"a line between each particle that is spawned" [doc], oldest first, one
quad per consecutive pair in the shader's path without geometry
shaders: four vertices sharing a_PositionVec4 (start position, start
size), a_TexCoordVec4 (end position, particle count), a_TexCoordVec4C1
(the point before the start, the pair's index), a_TexCoordVec4C2 (the
point after the end, end size), a_TexCoordVec4C3 (end colour) and
a_Color (start colour), and a_TexCoordC4 the corner (across, along)
[asset: genericropeparticle.vert]. The shader turns each end along its
neighbours and runs texture v from 1 at the oldest particle to 0 at the
newest; the rope's width (2 * size) lies on one side of the line, as the
shader computes it [asset]. `subdivision` n (default 0) splits each pair
into n + 1 pieces along a Catmull-Rom curve through the particles [doc:
"Higher numbers makes corners smoother"; inference: the curve].
`uvscale`, `uvscrolling`, `uvsmoothing` and `fadealpha` are not done
[corpus: absent or false].

Rope trails (`ropetrail` [corpus 30]) draw a rope per particle through
its own past positions [doc: "draws a line along the path of each
particle"]: the position is kept `segments` (default 8 [inference])
times per `length` seconds (default 0.5 [inference]), the rope runs
from the oldest kept position to the particle, and the shader, with
`TRAILRENDERER` set, gets g_RenderVar0 = (segments, 0, the fraction of
the keeping interval passed, segments) so the newest piece grows
smoothly [asset: genericropeparticle.vert TRAILRENDERER path].

## 7. Capability

The report (`../model/README.md` section 9) names `particles` for any
particle layer and adds, for any loaded particle file (children
included), `particle-controlpoints` (a component of the lists in
sections 4 and 5 that use control points, `inheritcontrolpointvelocity`,
an emitter around a control point other than 0, or a control point that
follows the pointer, is a scene position or copies its parent's),
`particle-children` (a `children` list) and `particle-trails` (a
`spritetrail`, `rope` or `ropetrail` renderer, or an orientation other
than `screen`). Phase P5a supported only `particles`, so the "auto"
engine kept linux-wallpaperengine for the rest; with P5b all four are
supported. Components no phase knows are load warnings ("particle
component NAME not supported") and are skipped. With P5a, 61 of the 208
scenes of 2026-10-31 were fully supported (39 before); 145 use
particles, of them 53 control points, 77 children and 81 trails
[corpus: `desktop-canvas-scene --check-all`].

## 8. Control points (P5b)

Each system has 8 control points, `0` always "tied to the origin
position of the particle system" [doc: Control Points]. Entries `{id,
flags, offset, locktopointer, parentcontrolpoint, angles}` [corpus 4731
entries]: `offset` "the (relative) position"; flag 1 locks the point
"to the mouse cursor" [doc; asset: the presets' cursor follow and avoid
examples set flag 1 on the point they use]; the older `locktopointer`
key alone does not [inference: 3636723645's point 1 says `locktopointer`
true without flag 1, and its layers override that point with a fixed
position across the screen, their particles' line; another scene keeps
an override on a flag 1 point that its controlpointattract uses as the
cursor]; flag 2 "Worldspace": the offset is a fixed scene position
[doc; inference: 11 of its 15 entries move the offset off 0]; flag 4
"Copy from parent" with `parentcontrolpoint` [doc; corpus 8/8]; flag 8
"Raw value" copies the parent's point without converting it
[doc: listed after copy from parent; inference: the bit]; flag 16
hides the editor gizmo [doc: last option; inference: the bit] and
changes nothing here.

Every step, before emitting, each point gets a position in the
system's space (section 2):
1. a point locked to the pointer: the pointer's scene position (the
   cursor through the view of `../render/README.md` section 1; the view
   centre before the first pointer event [inference]) brought into the
   system's space, also for point 0 [asset: presets interactive/trail_1
   locks point 0 and spawns at the cursor];
2. a point copying its parent (child systems, section 9): the parent's
   point `parentcontrolpoint` (default 0) in scene space, brought into
   this system's space (as is with flag 8);
3. point 0 otherwise: the system origin (its offset ignored [doc]);
4. a worldspace point: its offset as a scene position;
5. any other point: its offset in the system's space.
The layer overrides `controlpoint<n>` (vec3) replace point n's offset
(a point locked to the pointer keeps following it), in the space its
flags give it [doc: IParticleSystemInstance "Position
of control point n"; corpus: the 23 overrides of worldspace points hold
scene positions next to their objects]; `controlpointangle<n>` is kept
for scripts (phase P6). Child systems with "set control points" take
points from their parent's particles (section 9). A point's velocity is
its change since the last step over dt.

## 9. Children (P5b)

`children` entries `{id, name (a particle file), type, origin, angles,
scale, maxcount, probability, flags, controlpointstartindex}` [corpus
155; doc: Children]. A child is another particle system with its own
file, material and simulation, placed by the child entry's `origin`
("the position offset at which child particle systems are spawned"),
`angles` (z used) and `scale` relative to its spawn place [doc], and
drawn right after its parent [inference]. Types [doc]:
- `static` (also when `type` is absent: 106 of 155 [corpus; inference]):
  one instance "spawned once at the particle system origin", living with
  the parent; most are continuous emitters, extra systems sharing the
  layer [corpus: 192 of the 195 static child uses emit at a rate];
- `eventspawn`: an instance where a particle is born, "at the same time
  particles of this system spawn" [doc];
- `eventfollow`: an instance per particle that follows it ("created
  multiple times and follow individual particles"); when its particle
  dies it stops emitting [inference];
- `eventdeath`: an instance where a particle dies.
Event instances are created with `probability` (default 1) while fewer
than `maxcount` (default 10 [inference]) of that child are alive ("The
maximum number of child particle systems" [doc]) and end once they can
emit no more (bursts done, durations over, followed particle gone) and
their particles are gone [inference; corpus: all 35 eventspawn child
uses are bursts, the 40 eventfollow uses follow particles]. An instance
sits at its spawn place (the parent's origin, or the particle's
position) in the parent's space; a child file that is worldspace (flag
1) leaves its particles in the scene where they were born, so a
following child draws a trail [asset: presets fireflies], otherwise its
particles move with the instance. Flag 1, "set control points": the
child's points from `controlpointstartindex` (default 0) on take the
positions of the parent's live particles, oldest first, every step
[doc: "will use the positions of this system's particles as its control
points"; asset: presets thunderbolt_child_spawner sets flag 1 with
index 1]; other flags are not done. Children nest (presets thunderbolt:
three levels) up to 4 levels in this project; a file naming itself is
not nested again.

## Files

- `ParticleFile` (`parseParticleFile`): section 1 and the component
  lists, control points and children; components keep their JSON with
  typed getters (`number`, `vec3`, `integer`, `text`) that read strings,
  bools and single numbers like the files do. Never fails on content.
- `ParticleSystem`: sections 2 to 5 for one file: `step(dt)`,
  `prewarm()`, the layer factors (`setFactors`), the spawn transform of
  worldspace systems; `particleNeeds()` says what a file needs beyond
  P5a (section 7); `valueNoise()` is the noise of sections 4 and 5.
  Phase P5b adds control points (section 8: `setPointer`,
  `setControlPointOffset`, `controlPoint`), the control point
  components, children (section 9: `forEachSystem`, `childInstances`,
  `finished`, `stopEmitting`) and the rope trails' kept positions
  (section 6.1: `Particle::trail`, `trailFraction`);
  `particleRendering()` reads the renderer settings with their
  defaults.
- Drawing lives in `../gl/SceneRenderer.cpp` (particle layers) and the
  program request in `../shader/Programs.h` (`particleRequest`: the
  combos of section 6).

Library `canvas_scene_core` (no GPU). Tests:
`claude_tests/test_scene_particles` (parsing, needs, emission,
initializers, operators, determinism, program requests; P5b: control
points, sequences, the control point operators, children, rope trails,
renderer settings) and the particle part of
`claude_tests/test_scene_render` (drawing and blending, a static child
system and a rope, on every GL device).
