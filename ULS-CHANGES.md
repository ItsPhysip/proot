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
| 2026-09-27 | syscall: translate fchmodat2 (452): it reached the kernel untranslated, so guest paths failed and host-only paths were acted on | `src/syscall/sysnums*.{list,h}`, `src/syscall/enter.c`, `src/syscall/seccomp.c`, `src/extension/fake_id0/fake_id0.c` | ULS |
| 2026-09-28 | syscall: an empty path with AT_FDCWD and AT_EMPTY_PATH (fstatat, statx, fchownat, fchmodat2, utimensat, faccessat2, linkat, name_to_handle_at) acted on proot's *host* launch directory, because proot emulates the guest cwd; it is now translated as "." against the guest cwd. statx's exit handler no longer turns it into EBADF | `src/syscall/enter.c`, `src/tracee/statx.c` | ULS |
| 2026-09-28 | syscall: translate open_tree (428), which reached the kernel untranslated and returned fds on host paths (`/system`); the fd-based mount API (move_mount, fspick, mount_setattr, open_tree_attr, open_tree with OPEN_TREE_CLONE) returns ENOSYS so callers fall back to the emulated mount(2) | `src/syscall/sysnums*`, `src/syscall/enter.c`, `src/syscall/seccomp.c` | ULS |
| 2026-09-28 | syscall: translate AF_UNIX pathname destinations of sendto, sendmsg (msghdr copied, never written in place) and sendmmsg; they reached the kernel untranslated (host objects reachable, guest paths ENOENT) | `src/syscall/socket.{c,h}`, `src/syscall/enter.c`, `src/syscall/seccomp.c` | ULS |
| 2026-09-28 | fake_id0: chroot() replaced the name-space by freeing tracee->fs while its binding lists' destructor still needed it, aborting proot ("binding.c:438: Type mismatch"); the bindings are now replaced in place, and checked against the guest path | `src/extension/fake_id0/chroot.c`, `tests/test-897815a0.sh` | ULS |
| 2026-09-28 | sysnum: `translate_sysnum()` read one entry past the syscall table (`>` instead of `>=`) | `src/syscall/sysnum.c` | ULS |
| 2026-09-28 | socket: a translated AF_UNIX path too long for sun_path was bound to a temporary name through a Binding allocated on tracee->ctx but never unlinked from the binding lists (use-after-free; connect() then got ENOENT and the socket lived in the temp dir). connect/sendto/sendmsg now reach the socket through a short temporary symlink removed after the syscall; bind uses a symlink to the parent directory so the socket is created at its real path (getsockname/getpeername resolve it back), and only falls back to a binding, on life_context, when even that is too long | `src/syscall/socket.{c,h}`, `src/syscall/enter.c`, `src/path/temp.{c,h}` | ULS |
| 2026-09-28 | syscall: recvfrom and recvmsg (also via socketcall) reported a named AF_UNIX sender by its *host* path (or the short link of a long path); it is now detranslated to the guest path, truncated to the caller's addrlen with the full length reported. Abstract and unnamed senders are untouched. An address the kernel itself had to truncate (buffer smaller than the host name) is still left as is | `src/syscall/socket.{c,h}`, `src/syscall/enter.c`, `src/syscall/exit.c`, `src/tracee/tracee.h`, `tests/test-7ec0f5a1.c` | ULS |

Base: termux/proot `d4d2a19`.

## Verification

- 2026-09-28 fixes (empty path at AT_FDCWD, open_tree, send* destinations, chroot, sysnum bound): each shown on the S26 Ultra, without shims, against the previous release: host launch-dir/`/system`/canary objects were acted on or opened before, and are not after; chroot no longer aborts. The ULS device suite passes with the shims under `-0` and `-0 -l`.

- fchmodat2, run raw on the S26 Ultra with no shims: a guest path is changed (it was ENOENT before), `/system` gives ENOENT (it was EROFS from the host's /system), and a no-follow symlink gives ENOTSUP, as the kernel does.

- upstream `tests/test-5c2e7a91.sh`: passes. On the base, the file is destroyed when every suffix is taken, and a stale final file is overwritten.
- On a Samsung Galaxy S26 Ultra (Android 16, kernel 6.12), with a fresh Ubuntu 24.04 rootfs, `-l`, and the ULS shims: apt install/reinstall (3 rounds), groupadd/useradd, and `dpkg --audit` are all clean. With all 9999 suffixes taken, `ln` is refused with "Too many links" and the original is kept.
