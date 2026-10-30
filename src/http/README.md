# src/http/

The settings UI's server side.

- `HttpServer` minimal HTTP/1.1 on the daemon event loop, bound to
  127.0.0.1 only: one request per connection, Content-Length bodies (1 MB
  cap), nonblocking streamed file responses (previews never stall the loop),
  30 s idle timeout, at most 64 clients. Every request must carry Host
  `127.0.0.1:<port>` or `localhost:<port>` (DNS rebinding defence) and
  state changing requests with an Origin header must be same origin
  (cross-site defence); otherwise 403. `parseRequest`, `urlDecode`,
  `hostAllowed`, `originAllowed` and `mimeType` are pure functions covered
  by `claude_tests/test_http.cpp`.
- `WebApi` routes: JSON API under `/api/` (documented in `WebApi.h`) mapped
  onto `control/Controller`, `/preview/<id>` (only the item's own declared
  preview file, resolved inside its directory), and static UI files (plain
  file names only) from the directory found by `findUiDir()`:
  `$CANVAS_UI_DIR`, `<exe>/../share/desktop-canvas/ui`, the install prefix,
  then the source tree's `data/ui` for development builds.

Configured by config `ui` (`enabled`, `port`, default 8765); main.cpp starts
it and logs the address. A taken port is logged and the daemon runs without
the UI.
