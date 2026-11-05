#!/usr/bin/env bash
set -euo pipefail
WK=/home/wenyang.zhang/aiacc/ipset/1_ip/GPLPU/v1.0/3_tool/tvgen/load_store_v3/MatMul_NT_w4f16/fpga_workspace
DIST=$WK/dist
pack_one() { # $1 = pkg name (gate_up_dec_t0), $2 = bundle name (gate_up_dec)
  local N=$1 B=$2
  echo "[dist] $N pack start $(date +%H:%M:%S)"
  tar -cf - -C "$WK" \
    --transform="s|^pkgs/$N|$N|" \
    --transform="s|^bundles/$B|$N/tiles|" \
    --transform="s|^scripts/run_n_tiles.sh|$N/run_n_tiles.sh|" \
    --transform="s|^README_FPGA_LADDER6.md|$N/README.md|" \
    "pkgs/$N" "bundles/$B" "scripts/run_n_tiles.sh" "README_FPGA_LADDER6.md" \
    | pigz -p 16 > "$DIST/fpga_w4f16_$N.tar.gz"
  echo "[dist] $N done $(date +%H:%M:%S) -> $(du -h "$DIST/fpga_w4f16_$N.tar.gz" | cut -f1)"
}
pack_one gate_up_dec_t0 gate_up_dec
pack_one down_dec_t0   down_dec
pack_one gate_up_pre_t0 gate_up_pre
pack_one down_pre_t0   down_pre
( cd "$DIST" && sha256sum fpga_w4f16_*.tar.gz > SHA256SUMS.txt )
echo ALL_DIST_DONE
