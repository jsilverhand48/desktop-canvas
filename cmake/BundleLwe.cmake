# Optional bundling of linux-wallpaperengine for distros without a package.
#
# Downloads a pinned upstream source snapshot by URL (no git operations),
# builds it with ExternalProject, and installs the resulting binary next to
# desktop-canvas. Enable with -DDESKTOP_CANVAS_BUNDLE_LWE=ON.
#
# Note: linux-wallpaperengine has a heavy dependency set of its own (GLEW,
# SDL2, mpv, lz4, ffmpeg, pulseaudio, freetype, glslang, spirv-cross; it
# fetches CEF during its build). Those must be installed for this step to
# succeed; see its README. On Arch prefer the AUR package instead. The
# Flatpak manifest builds it as its own module.
include(ExternalProject)

# Pinned version tested against LweCommandBuilder's flag set.
set(CANVAS_LWE_VERSION "0.6.3" CACHE STRING
    "linux-wallpaperengine release to bundle")
set(CANVAS_LWE_URL
    "https://github.com/Almamu/linux-wallpaperengine/archive/refs/tags/v${CANVAS_LWE_VERSION}.tar.gz"
    CACHE STRING "Source tarball URL for the pinned release")

ExternalProject_Add(bundled-lwe
    URL "${CANVAS_LWE_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    CMAKE_ARGS
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
    INSTALL_DIR "${CMAKE_CURRENT_BINARY_DIR}/lwe-prefix"
)

install(PROGRAMS
        "${CMAKE_CURRENT_BINARY_DIR}/lwe-prefix/bin/linux-wallpaperengine"
        DESTINATION bin)
install(DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/lwe-prefix/share/"
        DESTINATION share OPTIONAL)
