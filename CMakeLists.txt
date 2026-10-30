# desktop-canvas: cross-desktop animated wallpaper daemon.
#
# Build options:
#   DESKTOP_CANVAS_WEB        Enable the optional Qt6 WebEngine backend for
#                             "web" type wallpapers (default OFF).
#   DESKTOP_CANVAS_BUNDLE_LWE Download and install a pinned linux-wallpaperengine
#                             release for "scene" wallpapers (default OFF; scenes
#                             otherwise use the binary found on PATH at runtime).
#   DESKTOP_CANVAS_SCENE      Build desktop-canvas-scene, the native scene
#                             renderer helper (default ON; skipped with a
#                             warning without libepoxy and libpng).
#   DESKTOP_CANVAS_TESTS      Build the test binaries under claude_tests/
#                             (default ON, skipped automatically when the
#                             directory is absent, as in release tarballs).
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
            src/display/wayland/WaylandPlatform.cpp
        )
        # Generated protocol headers stay private: the public display
        # headers only forward declare the protocol structs.
        canvas_add_wayland_protocols(canvas_display
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol/xdg-shell.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol/wlr-layer-shell-unstable-v1.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol/xdg-output-unstable-v1.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/protocol/fractional-scale-v1.xml
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
add_library(canvas_daemon STATIC
    src/config/Config.cpp
    src/source/WorkshopScanner.cpp
    src/source/Playlist.cpp
    src/source/SceneOptions.cpp
    src/source/LibraryWatcher.cpp
    src/render/Rotator.cpp
    src/render/InstanceManager.cpp
    src/audio/AudioPolicy.cpp
    src/process/ChildSupervisor.cpp
    src/process/LweCommandBuilder.cpp
    src/process/ExternalProcessInstance.cpp
    src/process/HelperLocator.cpp
    src/process/SceneHelperInstance.cpp
    src/render/NativeSupportCache.cpp
    src/ipc/IpcServer.cpp
    src/control/Controller.cpp
    src/http/HttpServer.cpp
    src/http/WebApi.cpp
)
target_link_libraries(canvas_daemon PUBLIC canvas_core)
# Settings UI files (data/ui): installed under share/desktop-canvas/ui; the
# source tree path lets a development build serve them without installing.
target_compile_definitions(canvas_daemon PRIVATE
    CANVAS_UI_INSTALL_DIR="${CMAKE_INSTALL_PREFIX}/share/desktop-canvas/ui"
    CANVAS_UI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/ui")

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

# Native scene helper (src/scene-host). canvas_scene_host holds the GL free
# part (pacing, surfaces, daemon protocol) so tests can link it; the
# executable adds the GL renderer, headless screenshots and the sandbox.
pkg_check_modules(EPOXY IMPORTED_TARGET epoxy)
pkg_check_modules(LIBPNG IMPORTED_TARGET libpng)
if(DESKTOP_CANVAS_SCENE AND TARGET canvas_display AND EPOXY_FOUND AND LIBPNG_FOUND)
    add_library(canvas_scene_host STATIC
        src/scene-host/FrameClock.cpp
        src/scene-host/SceneHost.cpp
    )
    target_link_libraries(canvas_scene_host PUBLIC canvas_core)
    add_executable(desktop-canvas-scene
        src/scene-host/main.cpp
        src/scene-host/PlaceholderRenderer.cpp
        src/scene-host/Screenshot.cpp
        src/scene-host/Sandbox.cpp
    )
    target_link_libraries(desktop-canvas-scene PRIVATE
        canvas_scene_host canvas_display PkgConfig::EPOXY PkgConfig::LIBPNG)
    install(TARGETS desktop-canvas-scene RUNTIME DESTINATION bin)
elseif(DESKTOP_CANVAS_SCENE)
    message(WARNING "desktop-canvas-scene not built: it needs a display "
                    "backend, libepoxy and libpng development files")
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
