#!/usr/bin/env bash
#
# mkdeb.sh — build the lpu-kmod .deb package.
#
# Builds lpu.ko through the driver Makefile, stages deb/lpu-kmod-<kver>/
# and packs it with dpkg-deb --root-owner-group, so it works entirely
# without root.
#
# Environment overrides:
#   KDIR     kernel build directory (default: /lib/modules/$(uname -r)/build)
#   VERSION  package version                      (default: 0.1.0)

set -euo pipefail

DRV_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KDIR="${KDIR:-/lib/modules/$(uname -r)/build}"
KVER="$(uname -r)"
VERSION="${VERSION:-0.1.0}"
ARCH="$(dpkg --print-architecture 2>/dev/null || uname -m)"

if [ ! -f "${KDIR}/Makefile" ]; then
    echo "kernel headers not found at ${KDIR}; install linux-headers or make KDIR=<kernel-build-dir>" >&2
    exit 1
fi

echo "==> building lpu.ko (KDIR=${KDIR})"
make -C "${DRV_DIR}" KDIR="${KDIR}"

KO="${DRV_DIR}/lpu.ko"
if [ ! -f "${KO}" ]; then
    echo "error: ${KO} was not built" >&2
    exit 1
fi

STAGE="${DRV_DIR}/deb/lpu-kmod-${KVER}"
OUT="${DRV_DIR}/deb/lpu-kmod-${KVER}.deb"

echo "==> staging ${STAGE}"
rm -rf "${STAGE}"
install -d "${STAGE}/DEBIAN" \
          "${STAGE}/lib/modules/${KVER}/updates"
install -m 0644 "${KO}" "${STAGE}/lib/modules/${KVER}/updates/lpu.ko"

sed -e "s|@VERSION@|${VERSION}|g" \
    -e "s|@KVER@|${KVER}|g" \
    -e "s|@ARCH@|${ARCH}|g" \
    "${DRV_DIR}/deb/control.in" > "${STAGE}/DEBIAN/control"

echo "==> packing ${OUT}"
dpkg-deb --root-owner-group -b "${STAGE}" "${OUT}"
echo "==> done: ${OUT}"
