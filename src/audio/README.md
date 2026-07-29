# src/audio/

`AudioPolicy`: the single source of truth for wallpaper audio. Global
default is muted (product requirement); per wallpaper overrides can be set
through IPC. Backends apply it as: mpv "mute" property (video), --silent
flag (scene child process, restart required), setAudioMuted (web).
