# desktop-canvas: cross-desktop animated wallpaper daemon.
#
# Build options:
#   DESKTOP_CANVAS_WEB        Enable the optional Qt6 WebEngine backend for
#                             "web" type wallpapers (default OFF).
#   DESKTOP_CANVAS_BUNDLE_LWE Download and install a pinned linux-wallpaperengine
#                             release for "scene" wallpapers (default OFF; scenes
#                             otherwise use the binary found on PATH at runtime).
#   DESKTOP_CANVAS_SCENE      Build desktop-canvas-scene, the native scene
#                             renderer helper, and its engine libraries
#                             (default ON; skipped with a warning without
#                             libepoxy, libpng, lz4 and libjpeg-turbo).
#   DESKTOP_CANVAS_TESTS      Build the test binaries under claude_tests/
#                             (default ON, skipped automatically when the
#                             directory is absent, as in release tarballs).
#   DESKTOP_CANVAS_FUZZ       Build the libFuzzer targets for the scene file
#                             readers (default OFF; clang only, and it adds
#                             AddressSanitizer and UndefinedBehaviorSanitizer
#                             to the whole build, so use its own build dir).
#
# Code is split into static libraries so the daemon, helper binaries and
# tests share it without relisting sources:
#   canvas_core     core/: the epoll event loop everything else runs on and
#                   the SEQPACKET control channel used with helpers.
#   canvas_display  gl/ EGL helpers, the display/ Wayland and X11 backends
#                   and the X11 pointer poller. Built when EGL plus Wayland
#                   (wayland-client, wayland-egl) or X11 (x11, xrandr) are
#                   found; exports CANVAS_HAVE_WAYLAND / CANVAS_HAVE_X11 to
#                   targets linking it. Does not need libmpv.
#   canvas_daemon   config, library scanning, instance management, child
#                   supervision, audio policy, IPC, control and the HTTP
#                   settings UI. Links no display or video library.
#   canvas_scene_props  scene/model/UserProperties: project.json user
#                   properties, shared by the daemon and the scene engine;
#                   no dependencies, always built.
#   canvas_scene_core  scene/: the native scene engine's GPU free part
#                   (asset readers: scene.pkg, file lookup, .tex parsing and
#                   decoding with lz4, libjpeg-turbo, libpng; the scene
#                   model and capability report).
#   canvas_scene_gl  scene/gl/: the engine's GL 3.3 code through libepoxy
#                   (the program cache; rendering from phase P4).
#   canvas_scene_host  scene-host/: the helper's GL free pacing, surface
#                   and daemon protocol code, linked by its tests.
# The desktop-canvas executable is main.cpp plus those libraries, and adds
# the in-process libmpv video backend (CANVAS_HAVE_VIDEO) when mpv is found;
# without libmpv the daemon still runs scene and web wallpapers. JSON and CLI
# parsing use vendored single header libraries in third_party/.
#
# Besides the binaries this installs the desktop entry, AppStream metainfo,
# application icon, systemd user unit, licence and the optional KDE Plasma
# integration packages. The first three are named after CANVAS_APP_ID below.
cmake_minimum_required(VERSION 3.20)
project(desktop-canvas VERSION 1.0.0 LANGUAGES C CXX)

# Reverse DNS application id. AppStream, the desktop entry and the icon must
# all carry this name, and Flatpak requires it to sit under a namespace the
# project controls (here the GitHub account hosting the repository). Changing
# it means renaming the matching files under packaging/.
set(CANVAS_APP_ID "io.github.jsilverhand48.DesktopCanvas")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DESKTOP_CANVAS_WEB "Enable Qt6 WebEngine backend for web wallpapers" OFF)
option(DESKTOP_CANVAS_BUNDLE_LWE "Bundle a pinned linux-wallpaperengine release" OFF)
option(DESKTOP_CANVAS_SCENE "Build the native scene helper desktop-canvas-scene" ON)
option(DESKTOP_CANVAS_TESTS "Build tests in claude_tests/" ON)
option(DESKTOP_CANVAS_FUZZ "Build libFuzzer targets (clang; sanitizes everything)" OFF)

if(DESKTOP_CANVAS_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "DESKTOP_CANVAS_FUZZ needs clang: "
                            "-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang")
    endif()
    # Coverage for the fuzzer and sanitizers for every library it links.
    add_compile_options(-fsanitize=address,undefined -fsanitize=fuzzer-no-link
                        -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_EGL IMPORTED_TARGET wayland-egl)
pkg_check_modules(EGL IMPORTED_TARGET egl)
pkg_check_modules(GL IMPORTED_TARGET gl)
pkg_check_modules(MPV IMPORTED_TARGET mpv)
pkg_check_modules(X11 IMPORTED_TARGET x11)
pkg_check_modules(XRANDR IMPORTED_TARGET xrandr)

include(WaylandProtocols)

# canvas_core: the event loop and the helper control channel. Its public
# include path (src, third_party) is inherited by every target that links a
# canvas_* library.
add_library(canvas_core STATIC
    src/core/EventLoop.cpp
    src/core/ControlChannel.cpp
    src/core/AudioSpectrum.cpp
)
target_include_directories(canvas_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# canvas_display: each backend needs EGL plus its own libraries. Neither
# needs libmpv; video is an optional backend of the daemon executable only.
set(CANVAS_HAVE_WAYLAND FALSE)
if(WAYLAND_CLIENT_FOUND AND WAYLAND_EGL_FOUND AND EGL_FOUND)
    set(CANVAS_HAVE_WAYLAND TRUE)
endif()
set(CANVAS_HAVE_X11 FALSE)
if(X11_FOUND AND XRANDR_FOUND AND EGL_FOUND)
    set(CANVAS_HAVE_X11 TRUE)
endif()

if(CANVAS_HAVE_WAYLAND OR CANVAS_HAVE_X11)
    add_library(canvas_display STATIC src/gl/EglContext.cpp)
    target_link_libraries(canvas_display PUBLIC canvas_core PkgConfig::EGL)
    if(GL_FOUND)
        target_link_libraries(canvas_display PUBLIC PkgConfig::GL)
    endif()
    if(CANVAS_HAVE_WAYLAND)
        target_sources(canvas_display PRIVATE
            src/display/wayland/WaylandConnection.cpp
            src/display/wayland/WaylandLayerOutput.cpp
            src/display/wayland/WaylandWindowOutput.cpp
            src/display/wayland/WaylandPlatform.cpp
            src/display/wayland/WaylandToplevels.cpp
        )
        # Generated protocol headers stay private: the public display
        # headers only forward declare the protocol structs.
        canvas_add_wayland_protocols(canvas_display
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol/xdg-shell.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol/wlr-layer-shell-unstable-v1.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol/xdg-output-unstable-v1.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol/fractional-scale-v1.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol/wlr-foreign-toplevel-management-unstable-v1.xml
        )
        target_link_libraries(canvas_display PUBLIC
            PkgConfig::WAYLAND_CLIENT PkgConfig::WAYLAND_EGL)
        target_compile_definitions(canvas_display PUBLIC CANVAS_HAVE_WAYLAND=1)
    endif()
    if(CANVAS_HAVE_X11)
        target_sources(canvas_display PRIVATE
            src/display/x11/X11Connection.cpp
            src/display/x11/X11DesktopOutput.cpp
            src/display/x11/X11Platform.cpp
            src/display/x11/X11Toplevels.cpp
            src/input/X11PointerPoller.cpp
        )
        target_link_libraries(canvas_display PUBLIC
            PkgConfig::X11 PkgConfig::XRANDR)
        target_compile_definitions(canvas_display PUBLIC CANVAS_HAVE_X11=1)
    endif()
else()
    message(WARNING "No display backend: install EGL plus the Wayland "
                    "(wayland-client, wayland-egl) or X11 (x11, xrandr) "
                    "development files. The daemon will only support --list.")
endif()

# canvas_daemon: everything the daemon does that is not display or video
# specific. Tests link this instead of relisting production sources.
# canvas_scene_props: project.json user properties (scene/model/
# UserProperties), shared by the daemon's SceneOptions and the native scene
# engine. Only the JSON header, so it is built whatever else is found.
add_library(canvas_scene_props STATIC src/scene/model/UserProperties.cpp)
target_link_libraries(canvas_scene_props PUBLIC canvas_core)

add_library(canvas_daemon STATIC
    src/config/Config.cpp
    src/source/WorkshopScanner.cpp
    src/source/Playlist.cpp
    src/source/SceneOptions.cpp
    src/source/LibraryWatcher.cpp
    src/render/Rotator.cpp
    src/render/InstanceManager.cpp
    src/audio/AudioPolicy.cpp
    src/audio/SpectrumAnalyzer.cpp
    src/audio/SpectrumService.cpp
    src/process/ChildSupervisor.cpp
    src/process/LweCommandBuilder.cpp
    src/process/ExternalProcessInstance.cpp
    src/process/HelperLocator.cpp
    src/process/SceneHelperInstance.cpp
    src/render/NativeSupportCache.cpp
    src/desktop/FullscreenPolicy.cpp
    src/ipc/IpcServer.cpp
    src/control/Controller.cpp
    src/http/HttpServer.cpp
    src/http/WebApi.cpp
)
target_link_libraries(canvas_daemon PUBLIC canvas_core canvas_scene_props)
# Settings UI files (data/ui): installed under share/desktop-canvas/ui; the
# source tree path lets a development build serve them without installing.
target_compile_definitions(canvas_daemon PRIVATE
    CANVAS_UI_INSTALL_DIR="${CMAKE_INSTALL_PREFIX}/share/desktop-canvas/ui"
    CANVAS_UI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/ui"
    CANVAS_APP_ID="${CANVAS_APP_ID}"
    CANVAS_KWIN_INSTALL_DIR="${CMAKE_INSTALL_PREFIX}/share/desktop-canvas/kwin")

# KDE bridge (desktop/KdeBridge): sd-bus from libsystemd, optional. Without
# it Plasma gets no fullscreen pause and no cursor under the desktop icons.
pkg_check_modules(LIBSYSTEMD IMPORTED_TARGET libsystemd)
if(LIBSYSTEMD_FOUND)
    target_sources(canvas_daemon PRIVATE src/desktop/KdeBridge.cpp)
    target_link_libraries(canvas_daemon PUBLIC PkgConfig::LIBSYSTEMD)
    target_compile_definitions(canvas_daemon PUBLIC CANVAS_HAVE_SDBUS=1)
else()
    message(WARNING "libsystemd not found: no KDE bridge (fullscreen pause "
                    "and cursor on Plasma Wayland)")
endif()

# Audio capture for scene audio responses (audio/PulseCapture): the
# PulseAudio protocol, which PipeWire serves too; optional. Without it
# audio responsive scenes read silence.
pkg_check_modules(LIBPULSE IMPORTED_TARGET libpulse)
if(LIBPULSE_FOUND)
    target_sources(canvas_daemon PRIVATE src/audio/PulseCapture.cpp src/audio/PlaybackWatcher.cpp)
    target_link_libraries(canvas_daemon PUBLIC PkgConfig::LIBPULSE)
    target_compile_definitions(canvas_daemon PUBLIC CANVAS_HAVE_PULSE=1)
else()
    message(WARNING "libpulse not found: no audio capture, audio responsive scenes stay still")
endif()
# The KWin script the bridge loads, with the bus names filled in. It lives
# next to the daemon binary in the build tree (KdeBridge::findScript).
string(REPLACE "." "/" CANVAS_BRIDGE_PATH "/${CANVAS_APP_ID}")
configure_file(data/kwin/desktop-canvas-bridge.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/kwin/desktop-canvas-bridge.js @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/kwin/desktop-canvas-bridge.js
        DESTINATION share/desktop-canvas/kwin)

add_executable(desktop-canvas src/main.cpp)
target_link_libraries(desktop-canvas PRIVATE canvas_daemon)
if(TARGET canvas_display)
    target_link_libraries(desktop-canvas PRIVATE canvas_display)
    # In process video: libmpv rendering into a display backend surface.
    if(MPV_FOUND)
        target_sources(desktop-canvas PRIVATE src/render/VideoBackend.cpp)
        target_link_libraries(desktop-canvas PRIVATE PkgConfig::MPV)
        target_compile_definitions(desktop-canvas PRIVATE CANVAS_HAVE_VIDEO=1)
    else()
        message(WARNING "libmpv not found: video wallpapers will not play")
    endif()
endif()

# Native scene engine core (src/scene): no GPU code, deterministic. The
# decoders' libraries stay private; static linking still hands them to
# whatever links canvas_scene_core.
pkg_check_modules(EPOXY IMPORTED_TARGET epoxy)
pkg_check_modules(LIBPNG IMPORTED_TARGET libpng)
pkg_check_modules(LZ4 IMPORTED_TARGET liblz4)
# 2.1 or later: the decoder uses the TurboJPEG 2.x interface with
# TJFLAG_LIMITSCANS (Debian 12 and Ubuntu 22.04 ship 2.1; 3.x keeps it).
pkg_check_modules(TURBOJPEG IMPORTED_TARGET libturbojpeg>=2.1)
# Text layers (src/scene/text): FreeType for fonts, fontconfig (optional)
# for system font names.
pkg_check_modules(FREETYPE IMPORTED_TARGET freetype2)
pkg_check_modules(FONTCONFIG IMPORTED_TARGET fontconfig)
# SceneScript (src/scene/script): QuickJS-ng, optional; without it scripts
# are reported unsupported.
find_path(QUICKJS_INCLUDE_DIR quickjs.h PATH_SUFFIXES quickjs-ng quickjs)
find_library(QUICKJS_LIBRARY NAMES qjs quickjs-ng)
if(QUICKJS_INCLUDE_DIR AND QUICKJS_LIBRARY)
    # A library that cannot link (for example one built for a newer C
    # library than the system's), or the original QuickJS rather than
    # QuickJS-ng, counts as absent.
    include(CheckCSourceCompiles)
    unset(CANVAS_QUICKJS_LINKS CACHE)  # checked again at every configure
    set(CMAKE_REQUIRED_INCLUDES ${QUICKJS_INCLUDE_DIR})
    set(CMAKE_REQUIRED_LIBRARIES ${QUICKJS_LIBRARY} m)
    check_c_source_compiles("#include <quickjs.h>
#ifndef QJS_VERSION_MAJOR
#error not QuickJS-ng
#endif
int main(void) { JSRuntime* rt = JS_NewRuntime(); JS_FreeRuntime(rt); return JS_GetVersion() ? 0 : 1; }"
        CANVAS_QUICKJS_LINKS)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
endif()
if(DESKTOP_CANVAS_SCENE AND LIBPNG_FOUND AND LZ4_FOUND AND TURBOJPEG_FOUND AND FREETYPE_FOUND)
    add_library(canvas_scene_core STATIC
        src/scene/assets/MappedFile.cpp
        src/scene/assets/PackageArchive.cpp
        src/scene/assets/AssetLocator.cpp
        src/scene/assets/TextureFile.cpp
        src/scene/assets/TextureDecode.cpp
        src/scene/model/Json.cpp
        src/scene/model/Field.cpp
        src/scene/model/Project.cpp
        src/scene/model/SceneFile.cpp
        src/scene/model/Resources.cpp
        src/scene/model/SceneLoader.cpp
        src/scene/model/Capabilities.cpp
        src/scene/shader/Preprocessor.cpp
        src/scene/shader/Translator.cpp
        src/scene/shader/Programs.cpp
        src/scene/shader/Tokens.cpp
        src/scene/shader/Syntax.cpp
        src/scene/shader/Conversions.cpp
        src/scene/render/Layout.cpp
        src/scene/render/Animation.cpp
        src/scene/particles/ParticleFile.cpp
        src/scene/particles/ParticleSystem.cpp
        src/scene/text/TextFont.cpp
        src/scene/text/TextLayout.cpp
        src/scene/script/ScriptEngine.cpp
        src/scene/audio/SoundLayer.cpp
    )
    target_link_libraries(canvas_scene_core
        PUBLIC canvas_core canvas_scene_props
        PRIVATE PkgConfig::LZ4 PkgConfig::TURBOJPEG PkgConfig::LIBPNG PkgConfig::FREETYPE)
    if(FONTCONFIG_FOUND)
        target_link_libraries(canvas_scene_core PRIVATE PkgConfig::FONTCONFIG)
        target_compile_definitions(canvas_scene_core PRIVATE CANVAS_SCENE_HAVE_FONTCONFIG)
    endif()
    if(QUICKJS_INCLUDE_DIR AND QUICKJS_LIBRARY AND CANVAS_QUICKJS_LINKS)
        target_include_directories(canvas_scene_core PRIVATE ${QUICKJS_INCLUDE_DIR})
        # libqjs leaves libm (sinh, cosh) to the program.
        target_link_libraries(canvas_scene_core PRIVATE ${QUICKJS_LIBRARY} m)
        target_compile_definitions(canvas_scene_core PRIVATE CANVAS_SCENE_HAVE_QUICKJS)
        message(STATUS "SceneScript: QuickJS at ${QUICKJS_LIBRARY}")
    elseif(QUICKJS_LIBRARY)
        message(WARNING "SceneScript: ${QUICKJS_LIBRARY} is not a QuickJS-ng that links (built for "
                        "a newer system? update the system), scene scripts unsupported")
    else()
        message(STATUS "SceneScript: QuickJS-ng not found, scene scripts unsupported")
    endif()
endif()

# canvas_scene_gl: the engine's GL code (src/scene/gl), GL 3.3 core
# through libepoxy: program cache, textures and the scene renderer.
if(TARGET canvas_scene_core AND EPOXY_FOUND)
    add_library(canvas_scene_gl STATIC
        src/scene/gl/ProgramCache.cpp
        src/scene/gl/Textures.cpp
        src/scene/gl/SceneRenderer.cpp
        src/scene/gl/VideoTexture.cpp
        src/scene/gl/SoundOutputMpv.cpp
    )
    # Video textures play through libmpv's render API when it is there.
    if(MPV_FOUND)
        target_link_libraries(canvas_scene_gl PRIVATE PkgConfig::MPV)
        target_compile_definitions(canvas_scene_gl PRIVATE CANVAS_SCENE_HAVE_MPV=1)
    else()
        message(STATUS "libmpv not found: scene video textures show a stand in")
    endif()
    target_link_libraries(canvas_scene_gl PUBLIC canvas_scene_core PkgConfig::EPOXY)
endif()

# Native scene helper (src/scene-host). canvas_scene_host holds the GL free
# part (pacing, surfaces, daemon protocol) so tests can link it; the
# executable adds the GL renderer, headless screenshots, the asset tools
# and the sandbox.
if(DESKTOP_CANVAS_SCENE AND TARGET canvas_display AND TARGET canvas_scene_gl)
    add_library(canvas_scene_host STATIC
        src/scene-host/FrameClock.cpp
        src/scene-host/SceneHost.cpp
    )
    target_link_libraries(canvas_scene_host PUBLIC canvas_core)
    add_executable(desktop-canvas-scene
        src/scene-host/main.cpp
        src/scene-host/AssetTools.cpp
        src/scene-host/SceneCheck.cpp
        src/scene-host/NativeRenderer.cpp
        src/scene-host/PlaceholderRenderer.cpp
        src/scene-host/Screenshot.cpp
        src/scene-host/Sandbox.cpp
    )
    target_link_libraries(desktop-canvas-scene PRIVATE
        canvas_scene_host canvas_scene_gl canvas_scene_core canvas_display PkgConfig::EPOXY
        PkgConfig::LIBPNG)
    install(TARGETS desktop-canvas-scene RUNTIME DESTINATION bin)
elseif(DESKTOP_CANVAS_SCENE)
    message(WARNING "desktop-canvas-scene not built: it needs a display "
                    "backend and the libepoxy, libpng, lz4 and "
                    "libjpeg-turbo development files")
endif()

# Control CLI. Talks to the daemon over its unix socket.
add_executable(desktop-canvasctl tools/canvasctl/main.cpp)
target_include_directories(desktop-canvasctl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Optional web wallpaper helper (Qt6 WebEngine + LayerShellQt on Wayland).
if(DESKTOP_CANVAS_WEB)
    find_package(Qt6 REQUIRED COMPONENTS Gui Quick WebEngineQuick)
    find_package(LayerShellQt QUIET)
    add_executable(desktop-canvas-web src/web-helper/main.cpp)
    target_link_libraries(desktop-canvas-web PRIVATE
        Qt6::Gui Qt6::Quick Qt6::WebEngineQuick)
    if(LayerShellQt_FOUND)
        target_link_libraries(desktop-canvas-web PRIVATE
            LayerShellQt::Interface)
        target_compile_definitions(desktop-canvas-web PRIVATE
            HAVE_LAYER_SHELL_QT=1)
    else()
        message(WARNING "LayerShellQt not found: web wallpapers will not "
                        "layer correctly on Wayland (X11 unaffected)")
    endif()
    install(TARGETS desktop-canvas-web RUNTIME DESTINATION bin)
endif()

if(DESKTOP_CANVAS_BUNDLE_LWE)
    include(BundleLwe)
endif()

install(TARGETS desktop-canvas desktop-canvasctl RUNTIME DESTINATION bin)
configure_file(packaging/systemd/desktop-canvas.service.in
               ${CMAKE_CURRENT_BINARY_DIR}/desktop-canvas.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/desktop-canvas.service
        DESTINATION lib/systemd/user)

# Desktop entry, AppStream metainfo and icon. AppStream ties the component id
# to the desktop file name, so these three basenames must stay identical.
install(FILES packaging/desktop/${CANVAS_APP_ID}.desktop
        DESTINATION share/applications)
install(FILES packaging/metainfo/${CANVAS_APP_ID}.metainfo.xml
        DESTINATION share/metainfo)
install(FILES packaging/icons/${CANVAS_APP_ID}.svg
        DESTINATION share/icons/hicolor/scalable/apps)
install(FILES LICENSE DESTINATION share/doc/${PROJECT_NAME})
install(DIRECTORY data/ui/ DESTINATION share/desktop-canvas/ui
        PATTERN "README.md" EXCLUDE)

# KDE Plasma integration: a transparent wallpaper plugin plus the KWin script
# that keeps the desktop containment above wallpaper layer surfaces. Installed
# system wide so packages carry them; packaging/plasma/setup-plasma-icons.sh
# remains the per user path for people building from source. Neither is
# activated by installing it, the script or the desktop settings still has to
# select them.
install(DIRECTORY packaging/plasma/org.desktopcanvas.transparent
        DESTINATION share/plasma/wallpapers)
install(DIRECTORY packaging/plasma/kwin-script/org.desktopcanvas.raisedesktop
        DESTINATION share/kwin/scripts)

# claude_tests/ is not tracked in git and so is absent from release tarballs.
# Skip it there rather than failing configure, which would otherwise break
# every distro build made from a tagged tarball.
if(DESKTOP_CANVAS_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/claude_tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(claude_tests)
elseif(DESKTOP_CANVAS_TESTS)
    message(STATUS "claude_tests/ not present, tests disabled")
endif()
