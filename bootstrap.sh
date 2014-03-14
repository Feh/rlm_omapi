#!/bin/sh

VERSION="${1:-4.2.2}"
BASENAME="dhcp-${VERSION}"
ARCHIVE="${BASENAME}.tar.gz"
BASEURL="http://ftp.isc.org/isc/dhcp"

wget -O"$ARCHIVE" "${BASEURL}/${ARCHIVE}" &&
    tar xfvz "$ARCHIVE" &&
    (
        cd "$BASENAME" &&
        export CFLAGS="-fPIC" &&
        ./configure &&
        make
    ) &&
    ln -sf "$BASENAME" isc-dhcp &&
    echo "\n\nAll done! Ready to compile the module."
