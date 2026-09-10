# third_party/

Vendored single header libraries (kept in tree so the project builds on
distros that do not package them):

- `nlohmann/json.hpp` nlohmann json v3.11.3 (MIT), licence text in
  `nlohmann/LICENSE.MIT`
- `CLI/CLI11.hpp` CLI11 v2.4.2 (BSD 3 clause), licence text in `CLI/LICENSE`

The licence texts are kept alongside the headers because both licences
require the copyright notice to be redistributed, and because Debian's
`debian/copyright` and Fedora's package review both audit bundled code file
by file.

Note that Debian and Fedora prohibit vendored convenience copies and package
both libraries themselves (`nlohmann-json3-dev` / `json-devel` and
`libcli11-dev` / `cli11-devel`). Submitting to either means teaching the
build to prefer a system copy and fall back to these headers.
