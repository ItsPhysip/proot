if [ ! -x ${ROOTFS}/bin/test-50c4e7a1 ]; then
    exit 125;
fi

DIR=/tmp/proot-test-50c4e7a1-xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

# The socket bound through a path too long for sun_path must be at
# its real place, not in PRoot's temporary directory.
rm -rf ${ROOTFS}${DIR}
${PROOT} -r ${ROOTFS} /bin/test-50c4e7a1 keep
test -S ${ROOTFS}${DIR}/s
rm -rf ${ROOTFS}${DIR}
