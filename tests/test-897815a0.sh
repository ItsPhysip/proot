if [ -z "$(which mcookie)" ] || [ -z "$(which chroot)" ] || [ -z "$(which sh)" ] || [ -z "$(which cp)" ] || [ ! -x ${ROOTFS}/bin/true ]; then
    exit 125;
fi

# Under -0, chroot(2) to a directory other than the current root is
# emulated by replacing the tracee's root binding.  It aborted PRoot
# ("Type mismatch: name[NULL] expected[Tracee]") whenever the tracee
# owned its bindings: the first tracee, or any tracee on its second
# chroot(2).

DIR=/tmp/$(mcookie).chroot
mkdir -p "${DIR}/new/bin" "${DIR}/new/mnt"
cp ${ROOTFS}/bin/true "${DIR}/new/bin/true"

# The first tracee.
if ${PROOT} -0 chroot "${DIR}/new" /bin/true; then FIRST=0; else FIRST=$?; fi

# A forked child (the shell must not exec chroot in place).
if ${PROOT} -0 sh -c "chroot ${DIR}/new /bin/true; exit \$?"; then CHILD=0; else CHILD=$?; fi

# A binding under the new root isn't supported: the chroot(2) has to
# be refused instead of dropping the binding.
MESSAGE=$(LC_ALL=C ${PROOT} -0 -b ${ROOTFS}/bin:"${DIR}/new/mnt" sh -c "chroot ${DIR}/new /bin/true; exit \$?" 2>&1) || true

rm -rf "${DIR}"

[ ${FIRST} -eq 0 ]
[ ${CHILD} -eq 0 ]
case "${MESSAGE}" in
    *"Operation not permitted"*) ;;
    *) exit 1 ;;
esac
