# data/ui/

The browser settings UI, served by the daemon (`src/http/`) at
http://127.0.0.1:8765/ and installed to `share/desktop-canvas/ui`. Plain
HTML, CSS and JavaScript: no framework, no build step.

- `index.html` page shell: header (now showing, rotation countdown, Next /
  Pause / Mute / Restart), Library and Settings tabs, wallpaper drawer.
- `app.js` all behaviour, talking to the JSON API documented in
  `src/http/WebApi.h`:
  - Library: preview grid with search and type / state / rating filters,
    per card and bulk enable for rotation. Previews of Mature and
    Questionable items are blurred until clicked (setting `ui.blur_mature`).
  - Wallpaper drawer: enable, show now on an output, clear the broken flag,
    and for scenes one editor per user property: dropdowns can be pinned or
    limited to the options random picks may use (NSFW options marked);
    sliders, toggles, colours and text can be overridden. Save can relaunch
    the wallpaper if it is showing. Scenes also get an engine choice
    (global, linux-wallpaperengine, auto, native), the renderer they use
    now, and when the native renderer has given up on them, why, with a
    "Retry native" button.
  - Settings: every global setting grouped (display, audio, rotation,
    scenes including the scene engine, fullscreen apps (pause, only while
    focused, ignored app ids, KDE bridge), screen spans (one comma
    separated output list per line), assignments, library folders, web,
    UI, broken list).
  - The status line marks outputs a fullscreen app has paused.
  - Status refreshes every 5 seconds. When its `library_revision` changes
    (the daemon rescanned because wallpaper folders were added to or
    removed from the Steam library) the grid reloads by itself and a toast
    says how many wallpapers were added or removed.
- `style.css` dark theme.

Text that comes from wallpapers (titles, tags, property labels) is always
set with `textContent`; Wallpaper Engine labels that contain markup are
reduced to plain text with DOMParser and never inserted as HTML.
