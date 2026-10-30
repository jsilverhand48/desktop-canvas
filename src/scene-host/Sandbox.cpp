// Implementation of applySandbox declared in Sandbox.h.
//
// Only write style filesystem rights are "handled" by the ruleset, so every
// read right stays unrestricted. Rights newer than the running kernel's ABI
// are masked out, as the Landlock documentation prescribes for best effort
// sandboxing. IOCTL_DEV (ABI 5) is deliberately left unhandled: GPU drivers
// issue device ioctls constantly.
#include "scene-host/Sandbox.h"

#include <fcntl.h>
#include <linux/landlock.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>

#include "core/Log.h"

namespace canvas::scenehost {

namespace {

constexpr uint64_t kWriteRightsV1 =
    LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR |
    LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_CHAR |
    LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |
    LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO |
    LANDLOCK_ACCESS_FS_MAKE_BLOCK | LANDLOCK_ACCESS_FS_MAKE_SYM;

int createRuleset(const landlock_ruleset_attr* attr, size_t size) {
    return static_cast<int>(
        syscall(SYS_landlock_create_ruleset, attr, size, 0));
}

bool addPath(int ruleset, const std::string& path, uint64_t rights) {
    int fd = open(path.c_str(), O_PATH | O_CLOEXEC);
    if (fd < 0) return false;  // missing paths are simply not granted
    landlock_path_beneath_attr beneath{};
    beneath.allowed_access = rights;
    beneath.parent_fd = fd;
    bool ok = syscall(SYS_landlock_add_rule, ruleset,
                      LANDLOCK_RULE_PATH_BENEATH, &beneath, 0) == 0;
    close(fd);
    return ok;
}

}  // namespace

int applySandbox(const std::vector<std::string>& writablePaths) {
    int abi = static_cast<int>(syscall(SYS_landlock_create_ruleset, nullptr, 0,
                                       LANDLOCK_CREATE_RULESET_VERSION));
    if (abi < 1) {
        log::warn() << "Landlock unavailable (" << strerror(errno)
                    << "), running without a sandbox";
        return 0;
    }

    uint64_t fsRights = kWriteRightsV1;
#ifdef LANDLOCK_ACCESS_FS_REFER
    if (abi >= 2) fsRights |= LANDLOCK_ACCESS_FS_REFER;
#endif
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    if (abi >= 3) fsRights |= LANDLOCK_ACCESS_FS_TRUNCATE;
#endif

    landlock_ruleset_attr attr{};
    attr.handled_access_fs = fsRights;
    size_t attrSize = sizeof(attr);
#ifdef LANDLOCK_ACCESS_NET_CONNECT_TCP
    if (abi >= 4)
        attr.handled_access_net =
            LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP;
#endif
#ifdef LANDLOCK_SCOPE_SIGNAL
    // Scoping (ABI 6) stays off: not needed for these goals, and leaving
    // attr.scoped zero keeps older kernels accepting the struct.
    attr.scoped = 0;
#endif

    int ruleset = createRuleset(&attr, attrSize);
    if (ruleset < 0) {
        log::warn() << "landlock_create_ruleset failed (" << strerror(errno)
                    << "), running without a sandbox";
        return 0;
    }
    for (const auto& path : writablePaths) addPath(ruleset, path, fsRights);
    // Device nodes: writes and truncation, never creation or removal.
    uint64_t deviceRights = LANDLOCK_ACCESS_FS_WRITE_FILE;
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
    if (abi >= 3) deviceRights |= LANDLOCK_ACCESS_FS_TRUNCATE;
#endif
    addPath(ruleset, "/dev", deviceRights);

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        syscall(SYS_landlock_restrict_self, ruleset, 0) != 0) {
        log::warn() << "landlock_restrict_self failed (" << strerror(errno)
                    << "), running without a sandbox";
        close(ruleset);
        return 0;
    }
    close(ruleset);
    log::info() << "sandbox active (Landlock ABI " << abi << ")";
    return abi;
}

}  // namespace canvas::scenehost
