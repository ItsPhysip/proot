# ULS changes to termux/proot

This branch (`uls`) is termux/proot with the changes below. It's what
[uls-proot](https://github.com/ItsPhysip/uls-proot) builds for
[ULS](https://github.com/Jaseunda/uls). proot is GPLv2 (see `COPYING`), and so
are these changes. Each change is a separate commit, and upstream PRs are
linked where they exist.

| Date | Change | Files | Source |
|---|---|---|---|
| 2026-09-06 | compat: include `<signal.h>` before the `SYS_SECCOMP` fallback (hosted builds only), so static glibc builds compile | `src/compat.h` | termux/proot#391 (ehfd) |
| 2026-09-27 | reg: read the ARM Thumb bit via `uregs[16] & 0x20`, so armv7 builds against glibc | `src/tracee/reg.c` | termux/proot#399 |
| 2026-09-10 | link2symlink: never lose the file when no intermediate name is free; refuse with EMLINK, roll back partial moves, report real errnos | `src/extension/link2symlink/link2symlink.c`, `tests/test-5c2e7a91.sh` | termux/proot#394 (tn-py) |
| 2026-09-10 | link2symlink: raise the intermediate suffix limit to 9999 | `src/extension/link2symlink/link2symlink.c` | termux/proot#394 (tn-py) |

Base: termux/proot `d4d2a19`.

## Verification

- upstream `tests/test-5c2e7a91.sh`: passes. On the base, the file is destroyed when every suffix is taken, and a stale final file is overwritten.
- On a Samsung Galaxy S26 Ultra (Android 16, kernel 6.12), with a fresh Ubuntu 24.04 rootfs, `-l`, and the ULS shims: apt install/reinstall (3 rounds), groupadd/useradd, and `dpkg --audit` are all clean. With all 9999 suffixes taken, `ln` is refused with "Too many links" and the original is kept.
