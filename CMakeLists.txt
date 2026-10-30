# desktop-canvas: cross-desktop animated wallpaper daemon.
#
# Build options:
#   DESKTOP_CANVAS_WEB        Enable the optional Qt6 WebEngine backend for
#                             "web" type wallpapers (default OFF).
#   DESKTOP_CANVAS_BUNDLE_LWE Download and install a pinned linux-wallpaperengine
#                             release for "scene" wallpapers (default OFF; scenes
#                             otherwise use the binary found on PATH at runtime).
#   DESKTOP_CANVAS_TESTS      Build the test binaries under claude_tests/
#                             (default ON, skipped automatically when the
#                             directory is absent, as in release tarballs).
#
# The daemon itself links Wayland, EGL, libmpv, and X11 libraries. JSON and
# CLI parsing use vendored single header libraries in third_party/.
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

# Core sources present in every build. Display and render backends are added
# below when their dependencies are available.
set(CANVAS_SOURCES
    src/main.cpp
    src/core/EventLoop.cpp
    src/config/Config.cpp
    src/source/WorkshopScanner.cpp
    src/source/Playlist.cpp
    src/source/SceneOptions.cpp
    src/source/LibraryWatcher.cpp
    src/render/Rotator.cpp
    src/audio/AudioPolicy.cpp
    src/process/ChildSupervisor.cpp
    src/process/LweCommandBuilder.cpp
    src/process/ExternalProcessInstance.cpp
    src/render/InstanceManager.cpp
    src/ipc/IpcServer.cpp
    src/control/Controller.cpp
    src/http/HttpServer.cpp
    src/http/WebApi.cpp
)

set(CANVAS_HAVE_WAYLAND FALSE)
if(WAYLAND_CLIENT_FOUND AND WAYLAND_EGL_FOUND AND EGL_FOUND AND MPV_FOUND)
    set(CANVAS_HAVE_WAYLAND TRUE)
    list(APPEND CANVAS_SOURCES
        src/display/wayland/WaylandConnection.cpp
        src/display/wayland/WaylandLayerOutput.cpp
        src/display/wayland/WaylandPlatform.cpp
    )
endif()

set(CANVAS_HAVE_X11 FALSE)
if(X11_FOUND AND XRANDR_FOUND AND EGL_FOUND AND MPV_FOUND)
    set(CANVAS_HAVE_X11 TRUE)
    list(APPEND CANVAS_SOURCES
        src/display/x11/X11Connection.cpp
        src/display/x11/X11DesktopOutput.cpp
        src/display/x11/X11Platform.cpp
        src/input/X11PointerPoller.cpp
    )
endif()

# Shared GL/video sources used by both display backends.
if(CANVAS_HAVE_WAYLAND OR CANVAS_HAVE_X11)
    list(APPEND CANVAS_SOURCES
        src/gl/EglContext.cpp
        src/render/VideoBackend.cpp
    )
endif()

add_executable(desktop-canvas ${CANVAS_SOURCES})
# Settings UI files (data/ui): installed under share/desktop-canvas/ui; the
# source tree path lets a development build serve them without installing.
target_compile_definitions(desktop-canvas PRIVATE
    CANVAS_UI_INSTALL_DIR="${CMAKE_INSTALL_PREFIX}/share/desktop-canvas/ui"
    CANVAS_UI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/ui")
target_include_directories(desktop-canvas PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if(CANVAS_HAVE_WAYLAND)
    canvas_add_wayland_protocols(desktop-canvas
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol/xdg-shell.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol/wlr-layer-shell-unstable-v1.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol/xdg-output-unstable-v1.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/protocol/fractional-scale-v1.xml
    )
    target_link_libraries(desktop-canvas PRIVATE
        PkgConfig::WAYLAND_CLIENT PkgConfig::WAYLAND_EGL)
    target_compile_definitions(desktop-canvas PRIVATE CANVAS_HAVE_WAYLAND=1)
endif()
if(CANVAS_HAVE_X11)
    target_link_libraries(desktop-canvas PRIVATE PkgConfig::X11 PkgConfig::XRANDR)
    target_compile_definitions(desktop-canvas PRIVATE CANVAS_HAVE_X11=1)
endif()
if(EGL_FOUND AND MPV_FOUND AND (CANVAS_HAVE_WAYLAND OR CANVAS_HAVE_X11))
    target_link_libraries(desktop-canvas PRIVATE PkgConfig::EGL PkgConfig::MPV)
    if(GL_FOUND)
        target_link_libraries(desktop-canvas PRIVATE PkgConfig::GL)
    endif()
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
