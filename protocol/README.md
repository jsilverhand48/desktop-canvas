# protocol/

Vendored Wayland protocol XML, turned into client headers at build time by
wayland-scanner (cmake/WaylandProtocols.cmake).

- `wlr-layer-shell-unstable-v1.xml` background layer surfaces. The
  get_layer_surface argument originally named "namespace" is renamed to
  "scope" here because namespace is a C++ keyword in the generated header;
  argument names do not affect the wire protocol.
- `xdg-shell.xml` dependency of the layer shell header (xdg_popup types).
- `xdg-output-unstable-v1.xml` output names and logical geometry.
- `fractional-scale-v1.xml` reserved for fractional HiDPI support.
- `wlr-foreign-toplevel-management-unstable-v1.xml` (from wlr-protocols,
  HPND style license in the file) other clients' windows with app id,
  outputs and fullscreen/activated state, for the fullscreen pause on
  wlroots compositors (`display/wayland/WaylandToplevels`). KWin does not
  offer it.
