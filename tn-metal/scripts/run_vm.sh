#!/usr/bin/env bash
# run_vm.sh — one-command launcher for the LPU vfio-user verification rig.
#
# Brings up the full three-QEMU E2E environment in the background:
#   scalar  executor instance : -M tn-card, -smp 8, fw-load-addr 0x80400000
#   card    ap instance        : -M tn-card, -smp 4, fw-load-addr 0x80000000
#                                + tn-accel-card EP model + vfio-user server
#   vm      x86 guest          : q35 + KVM + vfio-user-pci client + ssh fwd
#
# The two card-side QEMUs share an 8G DRAM model and a 4K mailbox as
# /dev/shm tmpfs files via memory-backend-file,share=on. All QEMU runs go
# through docker (the host lacks libfdt); the runtime image is qemu-run.
#
# Usage:
#   scripts/run_vm.sh [up]     # default: bring the whole rig up (full restart)
#   scripts/run_vm.sh down     # stop all three containers
#   scripts/run_vm.sh logs [scalar|card|vm]   # tail -F serial logs (default all)
#   scripts/run_vm.sh status   # container status + socket/log presence
#
# Environment:
#   TN_RIG_SSH_PORT   host ssh port forwarded to the VM (default 2222).
#                     Pick a unique port per person on a shared server.
#   TN_PREBUILT_DIR   dir with qemu-system-riscv64 / qemu-system-x86_64 /
#                     disk.qcow2 (default <repo>/prebuilt; see prebuilt/README.md)
#   TN_RIG_LOG_DIR  optional external serial-log directory.
#   TN_RIG_IMAGE      docker image to run QEMU with (default qemu-run)
#
# Per-user isolation on a shared server (concurrent rigs):
#   containers       tn-{scalar,card,vm}-<user>
#   /dev/shm files   tn-dram-<user>.bin (8G) / tn-mb-<user>.bin (4K; tn-card
#                     machine property mb-file -- the compiled-in default
#                     /dev/shm/tn-mb.bin is shared host-wide and would
#                     cross-talk between concurrent rigs)
#   socket + logs    build/rig/ inside each person's own clone
#
# `up` always performs a FULL restart (stop all three -> zero the shared
# files -> start in order scalar -> card -> vm). Never "partially" restart
# after swapping an operator DSO or fw: fence.i cannot be broadcast into
# already-running TCG translation blocks, so a stale scalar instance keeps
# executing the old image (false results). See docs/rt/lpu-sw-impl.md.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

SSH_PORT="${TN_RIG_SSH_PORT:-2222}"
PREBUILT_DIR="${TN_PREBUILT_DIR:-$ROOT/prebuilt}"
IMAGE="${TN_RIG_IMAGE:-qemu-run}"

U="$(id -un)"
C_SCALAR="tn-scalar-$U"
C_CARD="tn-card-$U"
C_VM="tn-vm-$U"

DRAM_FILE="/dev/shm/tn-dram-$U.bin"
MB_FILE="/dev/shm/tn-mb-$U.bin"
DRAM_SIZE=8G
MB_SIZE=4096

RIG_DIR="$ROOT/build/rig"
LOG_DIR="${TN_RIG_LOG_DIR:-$RIG_DIR/logs}"
SOCKET="$RIG_DIR/accel.sock"
# in-container mount points (repo always mounted at /work/tn-metal)
D_ROOT=/work/tn-metal
D_QEMU_RISCV="$D_ROOT/prebuilt/qemu-system-riscv64"
D_SOCKET="$D_ROOT/build/rig/accel.sock"

die() { echo "ERROR: $*" >&2; exit 1; }

# ---------------------------------------------------------------- preflight
preflight() {
  command -v docker >/dev/null 2>&1 \
    || die "docker not found; see the README quickstart (docker group setup)"
  docker ps >/dev/null 2>&1 || die "docker daemon not reachable (docker ps failed)
  - need docker group membership (one-time, by root):
      sudo usermod -aG docker \$USER   # then log out and back in
  - note: docker group membership is equivalent to root access"

  local missing=()
  [ -x "$PREBUILT_DIR/qemu-system-riscv64" ] || missing+=(qemu-system-riscv64)
  [ -x "$PREBUILT_DIR/qemu-system-x86_64" ]  || missing+=(qemu-system-x86_64)
  [ -f "$PREBUILT_DIR/disk.qcow2" ]          || missing+=(disk.qcow2)
  [ -f "$PREBUILT_DIR/libvfio-user.so.0" ]  || missing+=(libvfio-user.so.0)
  [ -f "$PREBUILT_DIR/pc-bios/bios-256k.bin" ] || missing+=(pc-bios/bios-256k.bin)
  if [ ${#missing[@]} -gt 0 ]; then
    echo "ERROR: missing in $PREBUILT_DIR: ${missing[*]}" >&2
    echo "  fix (copy from the prebuilt file owner, see prebuilt/README.md):" >&2
    echo "    cp <qemu-tree>/build/qemu-system-riscv64     $PREBUILT_DIR/" >&2
    echo "    cp <qemu-tree>/build-x86/qemu-system-x86_64  $PREBUILT_DIR/" >&2
    echo "    cp <any tn-metal clone>/prebuilt/disk.qcow2 $PREBUILT_DIR/  # VM disk, not built from the QEMU tree" >&2
    echo "    cp -a <qemu-tree>/build/subprojects/libvfio-user/lib/libvfio-user.so.0{,.0.1} $PREBUILT_DIR/" >&2
    echo "    mkdir -p $PREBUILT_DIR/pc-bios && cp <qemu-tree>/pc-bios/{bios-256k.bin,kvmvapic.bin,vgabios-stdvga.bin,efi-virtio.rom} $PREBUILT_DIR/pc-bios/" >&2
    exit 1
  fi

  local fw_missing=()
  [ -f "$ROOT/src/scalar/build-card/scalar_fw.bin" ] || fw_missing+=(src/scalar/build-card/scalar_fw.bin)
  [ -f "$ROOT/src/ap/build-card/ap_fw.bin" ]         || fw_missing+=(src/ap/build-card/ap_fw.bin)
  if [ ${#fw_missing[@]} -gt 0 ]; then
    echo "ERROR: missing card firmware: ${fw_missing[*]}" >&2
    echo "  fix: make -C src card" >&2
    exit 1
  fi

  # the per-user mailbox needs a QEMU build with the tn-card mb-file machine
  # property; a stale prebuilt binary would die at startup with "Property
  # 'tn-card.mb-file' not found" -- fail here with the exact fix instead.
  # (-M tn-card,help does not list instance-registered machine properties, so
  # probe the binary for the mb-file property description string instead.)
  if ! grep -qaF 'backing file for the cross-process mailbox' \
      "$PREBUILT_DIR/qemu-system-riscv64"; then
    die "prebuilt/qemu-system-riscv64 lacks the tn-card mb-file property; re-copy it from the qemu tree (see prebuilt/README.md)"
  fi
}

# KVM passthrough: /dev/kvm is root:kvm on the server; we reach it through
# the container device passthrough + supplementary kvm group.
kvm_args=()
KVM_DESC="off (TCG fallback, boot is slow)"
if [[ -e /dev/kvm ]]; then
  kvm_args=(--device /dev/kvm --group-add "$(stat -c %g /dev/kvm)")
  KVM_DESC="on"
fi

# extra mount when TN_PREBUILT_DIR points outside the repo tree (mounted
# over /work/tn-metal/prebuilt inside the container)
prebuilt_mount=()
if [ "$(cd "$PREBUILT_DIR" && pwd)" != "$ROOT/prebuilt" ]; then
  prebuilt_mount=(-v "$PREBUILT_DIR:$D_ROOT/prebuilt")
fi

# --------------------------------------------------------------------- down
cmd_down() {
  docker rm -f "$C_VM" "$C_CARD" "$C_SCALAR" >/dev/null 2>&1 || true
  echo "rig down: $C_VM $C_CARD $C_SCALAR removed"
}

# ----------------------------------------------------------------------- up
cmd_up() {
  preflight

  echo "== rig up (user: $U, ssh port: $SSH_PORT, KVM: $KVM_DESC) =="

  # full restart: stop own instances first (zeroing the shared files under a
  # running card instance would corrupt it)
  docker rm -f "$C_VM" "$C_CARD" "$C_SCALAR" >/dev/null 2>&1 || true

  mkdir -p "$LOG_DIR"
  rm -f "$SOCKET"

  # zero the per-user shared files. truncate only (keep inode): mapped /
  # soon-to-start instances must not silently split into two files. Content
  # is zeroed to reset stale values (ap fw static RP leftovers, mailbox
  # start=1 stop-cores flags all live in these files).
  for f in "$DRAM_FILE" "$MB_FILE"; do
    [ -e "$f" ] || touch "$f"
    truncate -s 0 "$f"
  done
  truncate -s "$DRAM_SIZE" "$DRAM_FILE"
  truncate -s "$MB_SIZE" "$MB_FILE"
  echo "shared files zeroed: $DRAM_FILE ($DRAM_SIZE), $MB_FILE ($MB_SIZE)"

  # 1) scalar executor instance: no -device/-object (owns no socket).
  #    -M tn-card property name is memory-backend (no -M memdev sugar), must
  #    pair with -m (the machine checks backend size == ram_size).
  #    /dev/shm must be bind-mounted: the container default is a private
  #    /dev/shm, without the mount the two instances silently split the
  #    "shared" file in two.
  #    LD_LIBRARY_PATH: the copied QEMU binary has no meson build-tree
  #    RPATH; libvfio-user.so.0 ships alongside it (prebuilt/README.md).
  docker run -d --name "$C_SCALAR" \
    -e LD_LIBRARY_PATH="$D_ROOT/prebuilt" \
    --user "$(id -u):$(id -g)" \
    -v "$ROOT:$D_ROOT" ${prebuilt_mount[@]+"${prebuilt_mount[@]}"} \
    -v "$LOG_DIR:$D_ROOT/build/rig/logs" \
    -v /dev/shm:/dev/shm \
    "$IMAGE" \
    "$D_QEMU_RISCV" \
      -M tn-card,memory-backend=tnmem,fw-load-addr=0x80400000,mb-file=$MB_FILE \
      -m 8G -smp 8 \
      -object memory-backend-file,id=tnmem,size=8G,mem-path=$DRAM_FILE,share=on \
      -bios "$D_ROOT/src/scalar/build-card/scalar_fw.bin" \
      -display none -serial file:"$D_ROOT/build/rig/logs/scalar.log" -monitor none
  echo "scalar up: $C_SCALAR (log: build/rig/logs/scalar.log)"

  # 2) card-side ap instance + EP model + vfio-user server socket for the VM
  docker run -d --name "$C_CARD" \
    -e LD_LIBRARY_PATH="$D_ROOT/prebuilt" \
    --user "$(id -u):$(id -g)" \
    -v "$ROOT:$D_ROOT" ${prebuilt_mount[@]+"${prebuilt_mount[@]}"} \
    -v "$LOG_DIR:$D_ROOT/build/rig/logs" \
    -v /dev/shm:/dev/shm \
    "$IMAGE" \
    "$D_QEMU_RISCV" \
      -M tn-card,memory-backend=tnmem,fw-load-addr=0x80000000,mb-file=$MB_FILE \
      -m 8G -smp 4 \
      -object memory-backend-file,id=tnmem,size=8G,mem-path=$DRAM_FILE,share=on \
      -bios "$D_ROOT/src/ap/build-card/ap_fw.bin" \
      -display none -serial file:"$D_ROOT/build/rig/logs/card.log" -monitor none \
      -device tn-accel-card,id=accel0 \
      -object x-vfio-user-server,id=tnsrv,type=unix,path=$D_SOCKET,device=accel0
  echo "card up: $C_CARD (log: build/rig/logs/card.log)"

  # wait for the vfio-user server socket before starting the VM client
  local i
  for i in $(seq 1 30); do
    [ -S "$SOCKET" ] && break
    sleep 1
  done
  if [ ! -S "$SOCKET" ]; then
    echo "ERROR: vfio-user socket $SOCKET did not appear within 30s" >&2
    echo "--- last 40 lines of card log: ---" >&2
    tail -n 40 "$LOG_DIR/card.log" >&2 || true
    cmd_down
    exit 1
  fi
  echo "vfio-user server socket ready: $SOCKET"

  # 3) x86 guest VM: q35 + KVM (TCG fallback) + virtio disk/net + ssh forward
  #    + vfio-user-pci client. The socket property is a SocketAddress struct,
  #    not a flat string: the short form socket=<path> fails with
  #    "Invalid parameter type for 'socket', expected: object" -- the JSON
  #    form below is required.
  docker run -d --name "$C_VM" \
    ${kvm_args[@]+"${kvm_args[@]}"} \
    -p "$SSH_PORT:2222" \
    --user "$(id -u):$(id -g)" \
    -v "$ROOT:$D_ROOT" ${prebuilt_mount[@]+"${prebuilt_mount[@]}"} \
    -v "$LOG_DIR:$D_ROOT/build/rig/logs" \
    "$IMAGE" bash -c '
      set -e
      SOCK="$1"
      [ -S "$SOCK" ] || { echo "socket missing: $SOCK" >&2; exit 1; }
      DEV=(-device "{\"driver\":\"vfio-user-pci\",\"socket\":{\"type\":\"unix\",\"path\":\"$SOCK\"}}")
      exec /work/tn-metal/prebuilt/qemu-system-x86_64 \
        -L /work/tn-metal/prebuilt/pc-bios \
        -machine q35 -m 4096 -smp 4 -cpu max \
        -accel kvm -accel tcg \
        -drive file=/work/tn-metal/prebuilt/disk.qcow2,if=virtio,format=qcow2 \
        -netdev user,id=net0,hostfwd=tcp::2222-:22 \
        -device virtio-net-pci,netdev=net0 \
        "${DEV[@]}" \
        -display none -serial file:/work/tn-metal/build/rig/logs/vm.log
    ' _ "$D_SOCKET"
  echo "vm up: $C_VM (log: build/rig/logs/vm.log)"

  cat <<EOF

== rig is up ==

serial logs:   scripts/run_vm.sh logs        (or tail -F build/rig/logs/*.log)
vm ssh:        ssh -o PasswordAuthentication=yes -o PubkeyAuthentication=no \\
                   -p $SSH_PORT chris@127.0.0.1    # password: 2333

deploy the driver source (host, once per driver source change):
    tar czf /tmp/tn-drv.tgz -C <repo> src/driver include
    scp -o PasswordAuthentication=yes -o PubkeyAuthentication=no \\
       -P $SSH_PORT /tmp/tn-drv.tgz chris@127.0.0.1:

in the VM, build the driver (once per source change; the image has
linux-headers + build-essential preinstalled):
    mkdir -p ~/tn-metal && tar xzf ~/tn-drv.tgz -C ~/tn-metal
    cd ~/tn-metal/src/driver && make

in the VM (after each VM boot, ko persists on the VM disk):
    sudo insmod ~/tn-metal/src/driver/lpu.ko

from the host, deploy the E2E test set (four artifacts):
    ssh -o PasswordAuthentication=yes -o PubkeyAuthentication=no \\
       -p $SSH_PORT chris@127.0.0.1 'mkdir -p ~/e2e'
    scp -o PasswordAuthentication=yes -o PubkeyAuthentication=no -P $SSH_PORT \\
      build/src/runtime/lpu_rt/tests/test_vecadd_dev \\
      build/src/runtime/lpu_rt/liblpu_rt.so.0.1.0 \\
      src/scalar/build-card/scalar_fw.elf \\
      src/scalar/sample_op/sample_op.so \\
      chris@127.0.0.1:~/e2e/

in the VM, run the E2E:
    cd ~/e2e && ln -sf liblpu_rt.so.0.1.0 liblpu_rt.so.0
    sudo env LD_LIBRARY_PATH=\$HOME/e2e LPU_TEST_FW_ELF=\$HOME/e2e/scalar_fw.elf \\
      LPU_TEST_DSO=\$HOME/e2e/sample_op.so ./test_vecadd_dev
    # expect: == VECADD E2E PASS ==

swapped an operator DSO, fw or the driver? FULL restart is mandatory:
    scripts/run_vm.sh down && scripts/run_vm.sh
stop the rig:
    scripts/run_vm.sh down
EOF
}

# -------------------------------------------------------------------- logs
cmd_logs() {
  local which="${1:-all}"
  local files=()
  case "$which" in
    scalar) files+=("$LOG_DIR/scalar.log") ;;
    card)   files+=("$LOG_DIR/card.log") ;;
    vm)     files+=("$LOG_DIR/vm.log") ;;
    all)    files+=("$LOG_DIR/scalar.log" "$LOG_DIR/card.log" "$LOG_DIR/vm.log") ;;
    *) die "unknown instance '$which' (scalar|card|vm)" ;;
  esac
  local existing=() f
  for f in "${files[@]}"; do
    [ -f "$f" ] && existing+=("$f")
  done
  if [ ${#existing[@]} -eq 0 ]; then
    die "no serial logs yet under $LOG_DIR (is the rig up?)"
  fi
  exec tail -F "${existing[@]}"
}

# ------------------------------------------------------------------ status
cmd_status() {
  echo "== rig status (user: $U, ssh port: $SSH_PORT) =="
  docker ps -a --format '{{.Names}}\t{{.Status}}' \
    | grep -E "^tn-(scalar|card|vm)-$U\b" \
    || echo "  (no rig containers for user $U)"
  [ -S "$SOCKET" ] && echo "socket:   $SOCKET (up)" \
                   || echo "socket:   $SOCKET (absent)"
  local n f
  for n in scalar card vm; do
    f="$LOG_DIR/$n.log"
    [ -f "$f" ] && echo "log:      $f ($(wc -l < "$f") lines)" \
                 || echo "log:      $f (absent)"
  done
  [ -e "$DRAM_FILE" ] && echo "shm:      $DRAM_FILE ($(stat -c %s "$DRAM_FILE") bytes)" \
                      || echo "shm:      $DRAM_FILE (absent)"
}

# ------------------------------------------------------------------- main
cmd="${1:-up}"
[ $# -gt 0 ] && shift || true
case "$cmd" in
  up)     cmd_up ;;
  down)   cmd_down ;;
  logs)   cmd_logs "$@" ;;
  status) cmd_status ;;
  *) awk 'NR > 1 && /^set -euo/ {exit} NR > 1 {sub(/^# ?/, ""); print}' "$0" >&2; exit 1 ;;
esac

