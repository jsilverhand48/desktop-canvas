// Self imposed restrictions for desktop-canvas-scene.
//
// Workshop content is untrusted (binary formats, shaders, JavaScript), so
// after the display connection and GL context exist the helper drops what it
// does not need, using Landlock (no privileges required):
// - Writing, creating, removing or renaming files is allowed only beneath
//   the given writable paths (caches, temp, runtime dir) and on device nodes
//   under /dev, which GPU drivers keep using. Reading stays allowed
//   everywhere: the drivers, fonts and the wallpaper itself live in many
//   places, and without the write and network rights a compromised helper
//   has nowhere to take what it reads.
// - TCP bind and connect are denied (Landlock ABI 4 or later). Connections
//   that already exist, such as the display server's, are unaffected.
// - PR_SET_NO_NEW_PRIVS is set first (required by Landlock, and it stops
//   setuid escalation through exec).
// On kernels without Landlock this logs and returns 0: the helper still runs,
// unsandboxed.
#pragma once

#include <string>
#include <vector>

namespace canvas::scenehost {

// Returns the Landlock ABI version the restrictions were applied with, or 0
// when none could be applied.
int applySandbox(const std::vector<std::string>& writablePaths);

}  // namespace canvas::scenehost
