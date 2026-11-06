# src/scene/puppet/

Puppet warp: image layers whose model names an MDL file are drawn as a
mesh bent by a skeleton (phase P8). This file is the specification;
evidence tags follow `../README.md`. Measurements come from
`claude_tests/corpus/puppet_*.py` over the user's library: 28 puppet
objects in 7 scenes, 21 distinct MDL files.

## 1. Where puppets come from

A model file's `puppet` names the MDL path (`../model/README.md` section
6) [corpus 23 models]. The image layer keeps its material, texture, size
and effects; only how the final pass reaches the screen changes (section
5). `animationlayers` on the object pick animations (`../model/README.md`
section 5.2) [req A1].

## 2. MDL files

Little endian. A file is a sequence of sections, each starting with a 9
byte tag: 4 letters, 4 decimal digits (the section version), NUL
[corpus 21/21]. Sections after the first are found by searching for the
tag text (`MDLS`, `MDAT`, `MDLA` followed by 4 digits and NUL) at or
after the end of the mesh data [req A5]; a missing section means its
content is absent. Every count and size is checked against the bytes
left; any failure keeps what was read so far (section 2.6).

### 2.1 Header and mesh (`MDLV`)

Versions seen: 13 (1 file), 16 (1), 17 (3), 23 (16) [corpus]. After the
tag: u32 format, u32, u32 (both 1), then the material path as NUL
terminated text [corpus 21/21]. Then, by version [corpus 21/21, each
ending exactly at the index data end]:

| Version | Before the vertex data | Vertex format word |
|---|---|---|
| 13 | u32 (0) | the header's |
| 16 | u32 (0), u32 format | that one |
| 17 and later | 7 x u32 (0 or 4096 first), u32 format | that one |

Then u32 vertex bytes, the vertices, u32 index bytes and u16 indices,
three per triangle; every index is below the vertex count [corpus
21/21]. Vertex count = vertex bytes / stride. Known formats [corpus]:

| Format | Stride | Layout (byte offsets) | Files |
|---|---|---|---|
| 0x9 | 20 | position 3 x f32 at 0, UV 2 x f32 at 12 | 1 (no skeleton) |
| 0x1800009 | 52 | position at 0, bone indices 4 x u32 at 12, weights 4 x f32 at 28, UV at 44 | 1 |
| 0x180000F | 80 | position at 0, normal at 12, tangent 4 x f32 at 24, bone indices at 40, weights at 56, UV at 72 [req A5] | 17 |
| 0x181000E | 84 | as 0x180000F with one extra f32 after the position (every offset after it + 4) | 2 (with blend shapes) |

An unknown format makes the file unreadable (feature
`puppet-unreadable`, unsupported). Version 23 files carry more data between
the indices and the next tag (26 bytes and up) [corpus]; it is skipped.

Positions are in pixels with the image centre at 0 and Y up; the UV of
a vertex is (0.5 + x / width, 0.5 - y / height) of the layer's image
size [corpus: sampled vertices of 4 formats agree to 3 digits], so the
rest mesh covers the image like the plain quad [req A5]. Normals,
tangents and the extra f32 are not used. z is kept as is.

### 2.2 Skeleton (`MDLS`)

Versions 1, 2, 4 [corpus]. u32 absolute file offset of the section end
(the next tag) [corpus 17/17], u32 bone count, then per bone: NUL
terminated name, u32 flags, i32 parent (-1 for a root; always a smaller
index than the bone [corpus 287/287]), u32 matrix size (64 [corpus
287/287]), the 4 x 4 f32 rest matrix, column major, relative to the
parent [req A5; corpus: last row 0 0 0 1 in 287/287], then NUL
terminated physics JSON (empty for 97 of 287 bones) [corpus]. Versions
2 and 4 have more bytes after the last bone; reading continues at the
section end offset.

Rest matrices are 2D [corpus: no X or Y rotation and no Z offset in
287/287]: translation = column 3 xy, angle = atan2(m[1], m[0]), scale =
(|column 0 xy|, |column 1 xy|) [req A5].

### 2.3 Attachments (`MDAT`)

u32 section end, u16 count, then per attachment: u16 bone index, NUL
terminated name, 4 x 4 f32 matrix relative to the bone [req A5; corpus:
10/10 parse to the section end, bone in range, last row 0 0 0 1]. An
attachment whose bone does not exist is dropped.

### 2.4 Animations (`MDLA`)

Versions 1, 4, 6 [corpus]. u32 section end, u32 animation count, then
per animation [corpus 47 animations]: i32 id (what `animationlayers`
name), u32 (0), NUL terminated name, NUL terminated mode (`loop`,
`mirror`, `single`), f32 fps, u32 length in frames, u32 (0), u32 bone
count (always the skeleton's), then per bone: u32 (0), u32 byte size and
size / 36 keyframes of 9 x f32: position xyz, Euler angles xyz in
radians, scale xyz, relative to the parent [req A5]. A track holds
length + 1 keyframes [corpus 2810/2810] and its first keyframe equals
the rest pose [corpus 2789 to 2808 of 2810 per component].

After the bone tracks come version specific bytes [corpus]:
- version 6: 5 zero bytes, u8 blend shape track count, per track u32
  (1), f32 (the shape's range, section 2.5), u32 (1), u32 byte size and
  size / 4 f32 weights, one per keyframe (length + 1, 0 to 1), then 29
  zero bytes [corpus 13/13 animations; 6 with one track];
- version 4: IK controller tracks, each u32 (0), u32 byte size and
  size / 36 keyframes like a bone track (section 3.4), as many as fit
  with length + 1 keyframes, then 10 zero bytes [corpus 7/7];
- version 1: 4 zero bytes.
Anything else is skipped: the next animation starts at
the first offset after the last track where an animation header parses
with a known mode, 0 < fps <= 1000, a name under 256 bytes and the
skeleton's bone count. An animation is kept only if fps > 0, length > 0
and its bone count matches [req A5].

### 2.5 Other sections

`MDLE` (version 2, 9 files) is not decoded [corpus].

`MDMP` (version 1, 2 files, both with format 0x181000E meshes) holds
blend shapes [corpus; doc: Shape Animations]: u32 section end, u16 shape
count, then per shape f32 range, u32 record count, u32 id, u32 (0), NUL
terminated name, u32 byte size and records of 3 x i16 (x, y, z; z is
always 0), then u32 byte size and that many bytes (all 0x7FFF, not
used) [corpus 2/2 parse to the section end]. A record is the offset
value / 32767 * range in mesh pixels (Y up) [corpus: offsets from 0.4 to
49 pixels downward on a 333 pixel image, the eyelid closing the docs
describe]. The extra f32 of format 0x181000E vertices is a 1 based
record index, 0 for vertices the shapes leave alone [corpus: exactly
record count vertices carry 1 to record count]; every shape indexes its
records this way [inference: one shape per file in the library].

### 2.6 Damaged files

No skeleton section: a static mesh (the version 16 file) [req A1].
Truncated or inconsistent skeleton: the bones read so far stay and every
animation is dropped [req A1]. One bad animation or attachment never
disables the others. Bounds: 16 Mi vertices, 4096 bones, 4096 animations;
anything beyond is a damaged file.

## 3. Pose

### 3.1 Time

Each animation layer has its own clock (like timeline animations,
`../render/README.md` section 10) that advances by dt * rate while
playing. Frame f = max(0, t * fps) [req A5]; `single`: min(f, length);
`mirror`: f mod (2 * length), mirrored back over the second half;
otherwise f mod length. Keyframes floor(f) and floor(f) + 1 (clamped to
length) are interpolated linearly per component [req A5].

### 3.2 Layers

Start at the rest pose. Visible layers apply in `animationlayers` order;
an unknown animation id or a weight of 0 is skipped [req A5]. A layer's
weight is its `blend`, times its fade (below). Normal layers move every
bone's position, angle and scale toward the keyframe by the weight;
additive layers add (key - rest) * weight to position and angle and
multiply scale by lerp(1, key / rest, weight) [req A5]. Only the Z angle
is used and z positions stay at rest (2D puppets, [req A5]; 89 of 276714
keyframes move z, 353 rotate about X or Y [corpus]).

Fades [inference: the editor's blend in, blend out and blend time]: a
layer that becomes visible with `blendin` fades from 0 to 1 over
`blendtime` seconds; one that becomes hidden with `blendout` fades from
1 to 0 and only then stops applying. Otherwise the fade is 1 while
visible.

### 3.3 Bones

Per bone, local = translate(position) * rotateZ(angle) * scale(scale);
world = parent world * local (roots: local) [req A5]. Then script
overrides (section 7) and physics (section 6) replace bone values before
their children are evaluated. Skin matrix = world * inverse(rest world)
[req A5].

### 3.4 Inverse kinematics

Bones whose physics JSON has `ik` true (3 bones in 3 files [corpus])
end a limb of `ikd` bones (2 in all). The bytes after the version 2 and
4 skeleton list the IK controllers; each animation has one track per
controller (section 2.4) [corpus]. The target track is the one whose
first keyframe position equals the IK bone's rest world origin, and its
Z angle equals the bone's rest world angle [corpus 3/3 exact]; another
track in the same animation is the pole [inference: it sits beside the
limb's middle joint].

Each frame, after the pose of section 3.3, the target position T and
angle are blended over the visible layers like a bone (rest: the rest
world origin and angle). The chain is the IK bone's parent B and, when
`ikd` is 2 and B has a parent, B's parent A. With A: the joint lengths
are the current world distances A to B and B to the IK bone; the
distance to T is clamped to what the limb reaches; the middle joint goes
to the side of the pole when there is one, else to the side it is on;
A and B turn by the angles that put the IK bone's origin on T (law of
cosines). Without A, B turns to point at T. Finally the IK bone's own
angle is set so its world angle equals the target angle, and the bones
are evaluated again from the new angles. Joint limits (`ikrmin`,
`ikrmax`) are not applied [inference: the docs page Limb Animations with
Inverse Kinematics describes target and orientation controllers, not the
solver].

### 3.5 Skinning

Blend shapes first [inference]: each shape's weight starts at 0 and
follows the visible layers like a bone channel (normal layers move it
toward the track's interpolated value by the layer weight, additive
layers add value * weight), clamped to 0..1; a script's
`setBlendShapeWeight` replaces it from then on. A vertex's rest position
gains the sum of weight * its record of each shape. Then, on the CPU
every frame a puppet is drawn: each vertex's position is the
sum over its up to 4 non zero weights of weight * (skin matrix of that
bone * rest position), not renormalised; its z stays the rest z [req
A5]. A bone index out of range counts as weight 0. Vertices with no
weight stay at rest. UVs never change.

## 4. Attachments

An object with `"attachment": name` whose `parent` is a puppet image
layer [corpus 22 objects: 11 images, 11 particles, all parented to
images] is placed in that attachment's frame: the frame is the bone's
world matrix (section 3.3) times the attachment matrix, in puppet space
(image centred, Y up, unscaled pixels). The child's origin xy is
transformed by the frame, the frame's Z angle (atan2 of its first
column) adds to the child's angle, and then normal parenting
(`../render/README.md` section 2) applies with the puppet layer as the
parent [req A6]. The latest evaluated pose is used (the rest pose before
the first frame). A parent that is not a puppet or an unknown name means
plain parenting [req A6]. Particle systems attached this way move with
the frame like any parented particle layer [req A9].

## 5. Drawing

The layer's material and effects run on the flat image exactly like a
plain image layer (`../render/README.md` sections 2 and 3); only the
final pass draws the mesh instead of the quad [req A5]. Mesh vertex
(x, y) maps to the layer rectangle the way the quad's corners do, so
(±width / 2, ±height / 2) land on the rectangle's corners; the texture
coordinate is the vertex UV, times the texture's usable fraction (u and
v max of section 2 of `../render/README.md`) when the final pass samples
the layer texture directly, and as is when it samples an effect result
(which fills its whole buffer) [req A5; corpus: mesh UVs are image
relative, section 2.1]. UVs are clamped to 0..1 first: meshes reach a
little past the image [corpus: UVs from -0.009 to 1.01], and those
vertices take the image edge instead of wrapping to the other side
[this project]. Blending is the material's, not forced [req A5];
face culling is off for every draw (already true for all layers), so
`cullmode` never hides a mirrored puppet [req A5]. Animated textures
apply the same fraction to their frame rectangle [req A5 gap].

## 6. Bone physics

Physics JSON keys [corpus 190 bones with JSON, 43 with simulation keys;
names matched to the options of the docs page Bone Physics Simulation by
meaning and value range, so the mapping is an inference]:

| Key | Meaning | Values seen |
|---|---|---|
| `r`, `t` | physics rotation, physics translation | bool |
| `se` | spring simulation (false: rigid) | bool |
| `rs`, `ts` | rotational, translational stiffness | 200, 300 |
| `rf`, `tf` | rotational, translational friction | 13 to 49 |
| `ri`, `ti` | rotational, translational inertia | 30 |
| `la`, `lamin`, `lamax` | limit rotation, min and max angles (radians, z used) | -pi, pi |
| `lt`, `ltmax` | limit translation, max distance (pixels) | false, 100 |
| `m`, `s`, `a` | tip mass, tip size (0: distance to the only child), tip forward direction | 20, 0, "1 0 0" |
| `ge`, `gd` | gravity enabled, gravity direction (x y) | false |
| `tm` | max torque | 200 |
| `ik`, `ik*` | inverse kinematics | 3 bones in 1 scene |

Simulation [inference; the docs give options, not formulas]: each
simulated bone keeps an offset from its animated value (Z angle for
rotation, xy position for translation) and a velocity, stepped at a
fixed 1/60 s (at most 8 steps per frame, dt clamped like every clock).
Per step, with k = stiffness / mass, c = friction / mass:
acceleration = -k * offset (spring only) - c * velocity + drive, where
drive comes from the animated motion of the bone's tip (the point at
tip size along the tip direction; without a tip size, the distance to
the only child, else 32 pixels): its acceleration, expressed in the
bone's frame and scaled by 1 / (1 + inertia / 10), pushes the offset the
other way (inertia). Rigid bones have no spring term and keep their
offset. Gravity, when enabled, adds a constant acceleration of
9.8 * 60 pixels/s² along `gd` at the tip. Offsets are clamped to the
limits (angle limits and max distance) and to ±pi and 4 * tip size
otherwise; a non finite value resets the bone. Torque is not limited.

Inverse kinematics follows section 3.4.

## 7. Scripts

`IImageLayer` and `IAnimationLayer` [doc: SceneScript reference]:

- `getAnimationLayerCount()`, `getAnimationLayer(name or index)` (an
  `IAnimationLayer` object or undefined), `createAnimationLayer(name,
  id or config)`, `playSingleAnimation(...)` (a new layer that removes
  itself after one run), `destroyAnimationLayer(...)`. A config object
  may hold `animation`, `rate`, `blend`, `visible`, `additive`.
- `IAnimationLayer`: read only `fps`, `frameCount` (length), `duration`
  (length / fps), `name`; read and write `rate`, `blend`, `visible`;
  `play()`, `stop()` (pause and frame 0), `pause()`, `isPlaying()`,
  `getFrame()`, `setFrame(frame)`, `addEndedCallback(fn)` (called each
  time a `single` layer reaches its end, or a loop wraps) [doc].
- Bones: `getBoneCount()`, `getBoneIndex(name)` (-1 when absent),
  `getBoneParentIndex(bone)`, `getBoneTransform(bone)` and
  `setBoneTransform(bone, Mat4)` (world, puppet space),
  `getLocalBoneTransform`, `setLocalBoneTransform`, `getLocalBoneAngles`,
  `setLocalBoneAngles`, `getLocalBoneOrigin`, `setLocalBoneOrigin`
  (parent relative) [doc]. Set values override the animated pose for
  that frame (scripts run after animation, so they win [doc: Interactive
  Puppet Warps]); `applyBonePhysicsImpulse(bone, directional, angular)`
  adds to the bone's physics velocity, `resetBonePhysicsSimulation(bone)`
  zeroes offsets and velocities.
- `getAnimation(name)` on a puppet layer with no timeline animation of
  that name returns the animation layer of that name, or with no name
  the first animation layer [inference: a library script plays
  `getAnimation()` of a puppet with no timeline animations; the
  `IAnimationLayer` methods are those of `IAnimation`].
- Blend shapes: `getBlendShapeIndex(name)` (-1 when absent),
  `getBlendShapeWeight(shape)` (the current weight),
  `setBlendShapeWeight(shape, weight)` (section 3.5) [doc].
- `ILayer.getAttachmentIndex(name)`, `getAttachmentMatrix`,
  `getAttachmentOrigin`, `getAttachmentAngles` (world values of the
  section 4 frame) [doc].

Layers start playing when visible; a layer whose `visible` is bound or
scripted re-evaluates every frame like any field.

## 8. Capability

`puppet` is supported, with blend shapes and inverse kinematics from
phase P9a. A missing MDL file is a file error and the layer draws as the
plain image, which is the rest pose (1 object [corpus]).
`puppet-unreadable` (the MDL cannot be read) is not supported. The scene
loader reads every model's MDL once (`LoadedScene::puppets`).

## Files

- `MdlFile.h`, `MdlFile.cpp`: section 2, the MDL reader.
- `Skeleton.h`, `Skeleton.cpp`: sections 3 and 6, layers, pose,
  inverse kinematics, physics, blend shapes and skinning.
