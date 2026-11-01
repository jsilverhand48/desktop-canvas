# src/scene/audio/

Specification of audio for scene wallpapers (plan phase P7): the audio
spectrum scenes react to, sound layers, muting while other applications
play, and the media information scripts receive. Evidence tags as in
`../README.md`. Corpus numbers come from
`claude_tests/corpus/audio_survey.py` over the workshop library of
2026-11-01 (208 scenes). Parts of this live outside the engine: the
capture, auto mute and media watching run in the daemon (`src/audio/`),
which hands their results to the helper.

## 1. The spectrum scenes read

Shaders read six engine arrays [doc: Shader Variables & Constants]:
`g_AudioSpectrum16Left[16]`, `g_AudioSpectrum16Right[16]` and the 32 and
64 versions: "Audio samples in positive range (not normalized)", "From
low frequencies at 0 to high frequencies until the end of the array",
one channel each. Package shaders of 96 scenes declare them (the 16 band
arrays in 96, the 32 and 64 band ones in 31) [corpus]; WE's own `pulse`
and `shake` effects do behind their `AUDIOPROCESSING` combo
[asset: effects/pulse/shaders/effects/pulse.vert], which materials and
effect instances set to 3 (26 times), 1 (2), 2 (1) or 0 (4) [corpus].

Scripts call `engine.registerAudioBuffers(resolution)` with 16, 32 or 64
and get an `AudioBuffers` object whose `left`, `right` and `average`
Float32Arrays of that length are "updated for every frame
automatically"; `average` is the mean of both channels; index 0 is low
bass, the last index high treble; values range "from 0.00 to 1.00" but
can exceed 1 [doc: AudioBuffers; Processing Audio Data with
SceneScript]. 35 scenes register buffers, asking for 64 bands 58 times
and 16 bands 36 times [corpus].

Particles react through `audioprocessing*` keys of emitters and of the
vortex and turbulence operators (section 4) [doc: Particle Component -
Emitters, Operators]. The project flag `supportsaudioprocessing` (55
projects) is the editor's note for web wallpapers [doc: web Audio
Visualization: the editor adds it when a listener is registered]; this
engine does not need it.

## 2. Our spectrum [this project]

Wallpaper Engine documents what the values mean, not how it computes
them, so this project defines its own analysis to meet the documented
shape (bass first, mostly 0..1, rarely above 1):

- **Capture**: what the user hears, the monitor of the default output
  (`@DEFAULT_MONITOR@` of the PulseAudio protocol, served by PipeWire and
  PulseAudio alike), 48000 Hz, two channels, 32 bit floats, read in 10 ms
  fragments.
- **Analysis**, 60 times a second over the newest 2048 samples of each
  channel (43 ms): Hann window, FFT, amplitude per bin `2 |X[k]| / sum
  of the window` (a full scale sine reads 1).
- **Bands**: 64 bands with edges `40 * 400^(i / 64)` Hz, i = 0..64 (40 Hz
  to 16 kHz, logarithmic like hearing). A band's amplitude is the
  largest bin amplitude inside it, or, for bands narrower than a bin, the
  amplitude interpolated at its centre frequency.
- **Level**: `max(0, 1 + 20 log10(amplitude) / 60)`: -60 dBFS and below
  read 0, a full scale sine 1, so quiet treble stays visible next to loud
  bass.
- **Time**: a band rises at once and falls with a 100 ms time constant
  (`level = max(new, old * exp(-dt / 0.1))`), which removes window
  flicker without hiding beats; the 32 and 16 band arrays are the means
  of 2 and 4 neighbouring 64 band values.
- Without capture (turned off, nothing wanted, no sound server, a
  standalone helper) every value is 0.

## 3. Sharing the spectrum

One capture serves every helper. The daemon owns a memfd of 4 KiB laid
out as (little endian): `u32 magic` 0x55414344 ("DCAU"), `u32 version`
1, `u32 sequence`, `u32 flags` (bit 0: capturing), then 64 left, 64
right, 32 left, 32 right, 16 left and 16 right floats. Writers make the
sequence odd, write, make it even again (a seqlock); readers copy and
retry while it is odd or changed. The daemon seals the memfd against
resizing and new writable mappings, so a helper can only map it read
only, and passes it at spawn as fd 4 with `--audio-fd 4`.

A helper whose scene reads the spectrum (a program using one of the
arrays, a script that registered buffers, an audio responsive particle
component) tells the daemon `{"type":"audio","wanted":true}`
(`process/HelperProtocol.h`). The capture runs while `audio.processing`
is on (config, default true; lwe's `--no-audio-processing` [bb]) and some
running, unpaused helper wants it; otherwise it stops and the values
fall to 0.

## 4. Audio response

WE's own effects turn the spectrum into one response per frame
[asset: effects/pulse/shaders/effects/pulse.vert, CreateAudioResponse]:
the mean of the 16 band values from band `min` to band `max` of the
left channel (mode 1), the right (mode 2) or both (mode 3, the mean of
all of them), then `smoothstep(bounds.x, bounds.y, r)`, then
`saturate(pow(r, exponent)) * amount`. Particles use the same response
over the 16 band arrays [doc: "The values go from 0 to 15, where 0 is
bass sounds and 15 higher frequency treble sounds"] with their keys
`audioprocessingmode` (0 off, 3 "Center": both channels [doc]; 1 and 2
left and right like the combo [asset]), `audioprocessingfrequencystart`
and `...end` (default 0 and 1, the effect's defaults [asset]),
`audioprocessingbounds` (default "0.5 1" [asset]) and
`audioprocessingexponent` (default 1 [asset]), amount 1:

- an emitter's rate is multiplied by the response: "The emitter will
  only be active when audio is playing" [doc], with the bounds deciding
  "at what points the audio response starts and stops" [doc];
- a vortex's speed is multiplied by the response: "ties the particle
  speed to audio playback, causing the vortex to stop spinning when no
  audio is being played" [doc];
- turbulence's audio phase factor is not done (no corpus use).

The corpus has 3 audio responsive components (two emitters and a
vortex, all mode 3) and 5 more that carry the keys with mode 0 or
without a mode [corpus].

## 5. Sound layers

An object with `sound` is a sound layer [corpus: 41 objects in 38
scenes, one per scene but one scene with 4]: `sound` lists files looked
up like any other (package, item folder, assets) [corpus: 38 lists of
one, others of 3, 4 and 6 files; MP3 layer 3 at 44.1 (40), 48 (8) and
32 kHz (2), all stereo, and one 16 bit PCM WAV]:

- `playbackmode`: `loop` (40) plays the list in order and starts over,
  without end ("By default sounds play in a loop" [doc: SceneScript
  Events in Timeline Animations]); `single` (1) plays the list once and
  stops ("so that your sound only plays once" [doc: Mouse Cursor Click
  Events]); `random` (none in the corpus) waits a random time between
  `mintime` and `maxtime` seconds (always 1 and 5 [corpus]) after each
  sound, then plays a random entry [inference from the key names];
- `startsilent` (true once): the layer waits for a script's `play()`
  ("otherwise your sound will play once when the wallpaper is loaded"
  [doc: Mouse Cursor Click Events]);
- `volume` (0..1; plain 40 times, animated once [corpus]) scales the
  layer's amplitude, on top of the wallpaper volume and mute the daemon
  sends; `muteineditor` concerns the editor and is ignored;
- scripts drive a layer through `ISoundLayer` [doc]: `play()` (start or
  resume), `pause()`, `stop()` (stop and rewind), `isPlaying()` and
  `volume` (the field); no corpus script does [corpus];
- sounds pause with the wallpaper (fullscreen pause, covered outputs)
  and resume with it.

Playback goes through libmpv, like video textures (`../gl/
VideoTexture.h`): one player per layer, audio only, reading the bytes
through mpv's stream callbacks, no config files. mpv's volume is the
wallpaper volume (the daemon's 0..100 scale, mpv's cubic curve, as video
wallpapers use it) times the cube root of the layer volume, so the layer
volume scales amplitude linearly [inference: a plain gain]. Without
libmpv the `sound` feature is unsupported.

## 6. Muting while other applications play

lwe mutes "when an app is playing sound" unless told otherwise [bb:
`--noautomute`]. The daemon watches the sound server's playback streams:
a stream counts when its process is neither the daemon nor one of its
descendants (helpers, their sound players, web helpers) and it is not
corked. While one plays, wallpaper audio is muted (sound layers, video
wallpapers, web wallpapers: the same path as the mute setting); one
second after the last stops, the setting applies again. `audio.auto_mute`
(config, default true) turns this off.

## 7. Media information

Scripts may export `mediaStatusChanged`, `mediaPlaybackChanged`,
`mediaPropertiesChanged`, `mediaThumbnailChanged` and
`mediaTimelineChanged` [doc: lib.sceneScript.d.ts; corpus: 28, 22, 16 and
4 scripts export the last four]. Wallpaper Engine takes the data from
Windows' media session [doc: Working with Media Playback Data]; here
the daemon reads Linux media players over MPRIS (the session bus names
`org.mpris.MediaPlayer2.*`), following the player that plays (the one
that started last), else the last paused one, and sends helpers
`{"type":"media", ...}` whenever something changes, and the position
once a second while playing (MPRIS does not signal it). Events:

- `mediaStatusChanged({enabled})` [doc: "when the media integration is
  turned on or off"]: enabled while `audio.media` (config, default true)
  is on;
- `mediaPlaybackChanged({state})`: `MediaPlaybackEvent.PLAYBACK_STOPPED`
  0, `PLAYING` 1, `PAUSED` 2 [doc; asset: baseclasses.js]; stopped when
  no player is left;
- `mediaPropertiesChanged({title, artist, subTitle, albumTitle,
  albumArtist, genres, contentType})` [doc]: MPRIS `xesam:title`,
  `xesam:artist` (joined with ", "), empty, `xesam:album`,
  `xesam:albumArtist`, `xesam:genre` (joined with ", "), "audio";
- `mediaThumbnailChanged({hasThumbnail, primaryColor, secondaryColor,
  tertiaryColor, textColor, highContrastColor})` [doc]: from
  `mpris:artUrl` when it is a local file (other schemes: no thumbnail;
  the engine fetches nothing); the helper decodes the image (PNG or
  JPEG), reduces it to 64 x 64 and clusters its colours (k-means, k = 3,
  largest first: primary, secondary, tertiary); `textColor` is the
  secondary or tertiary colour when its contrast ratio with the primary
  is at least 4.5, else black or white ("guaranteed to have sufficient
  contrast with primary color. May be secondary or tertiary color when
  possible" [doc]); `highContrastColor` is black or white, whichever
  contrasts more with the primary [doc];
- `mediaTimelineChanged({position, duration})` [doc: "Sent frequently
  while media is playing"]: MPRIS `Position` and `mpris:length` in
  seconds.

A script receives the current state (status, then playback, properties,
thumbnail and timeline when known) right after its first update
[inference: a scene started during playback must show it].

## 8. Capability

`sound` (`../model/README.md` section 9) is supported from phase P7b in
builds with libmpv. The spectrum, audio responses and media events need
no feature: without capture or media players they read silence and
"stopped".

## Files

(added with the implementation)
