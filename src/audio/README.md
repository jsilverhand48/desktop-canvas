# src/audio/

The daemon's audio: what wallpapers may play, and what scenes may hear.

- `AudioPolicy`: the single source of truth for wallpaper audio. Global
  default is muted (product requirement); per wallpaper overrides can be
  set through IPC. Backends apply it as: mpv "mute" property (video),
  "mute" messages (scene helper), setAudioMuted (web).
- `SpectrumAnalyzer` (no I/O): stereo frames to `AudioSpectrum` levels,
  per `src/scene/audio/README.md` section 2: 60 analyses a second over
  the newest 2048 frames (Hann window, one complex FFT for both
  channels), 64 logarithmic bands from 40 Hz to 16 kHz (largest bin
  inside, or interpolated at the centre of bands narrower than a bin),
  dB levels (-60 dBFS reads 0, a full scale sine 1), a 100 ms release,
  and the 32 and 16 band means.
- `SpectrumService`: the one capture shared by every native scene
  helper (section 3 of that spec). It owns the spectrum memory
  (`core/AudioSpectrum.h`; helpers get its read only descriptor as fd 4
  through `InstanceManager`) and runs the capture while config
  `audio.processing` is on and some helper reported demand
  (`setDemand`, keyed by instance tag: the scene reads the spectrum and
  the helper runs unpaused). Frames arrive on the backend's thread and
  are analysed and published there; stopping or failing publishes
  silence, and failures retry after 5 s while still wanted.
  `AudioCaptureBackend` is the seam to the sound server.
- `PulseCapture` (with libpulse, `CANVAS_HAVE_PULSE`): the backend over
  the PulseAudio protocol, which PipeWire serves too: a record stream on
  `@DEFAULT_MONITOR@` (what the default output plays), 48 kHz stereo
  float, 10 ms fragments, on libpulse's threaded mainloop. Nothing is
  written anywhere: the levels are all that leave it.
- `PlaybackWatcher` (with libpulse): muting while other applications
  play (spec section 6). `othersPlaying()` is the rule (an uncorked
  playback stream whose process is not the daemon or a descendant, found
  through `/proc` parents); the watcher subscribes to sink input events,
  re-applies the rule after each and reports on the loop: true at once,
  false after 1 s of quiet; a lost server is retried every 10 s.
  main.cpp hands the verdict to `InstanceManager::setOthersPlaying`.

Tests: `claude_tests/test_audio_spectrum` (analyzer on synthetic sines,
the shared memory, the service with a fake backend, the other playback
rule; a real capture and watcher with `CANVAS_PULSE_TEST=1`) and the
audio parts of `test_engine_selection`.
