#!/usr/bin/env bash
# FPGA host 端 N-tile 循环（在 U280 主机上运行）。
#
# 用法:  ./run_n_tiles.sh <n_tiles> [start_tile]
#   放在打包目录里与 run_one_case.sh 同级；tile bundle 树由 scp/rsync 放到 ./tiles/
#   （或用环境变量 TILES_ROOT 指向）。每 tile：换 hbm_data 段目录 + golden + meta，
#   然后整体重跑 run_one_case.sh（自带 SMI soft reset + 40 段载图 + 全 stage + stitched 比对）。
#
# bundle 目录来自 fpga_workspace/bundles/<shape>/tile_XXXX/（mk_tile_bundle.py 生成）。
set -uo pipefail

PKG=$(cd "$(dirname "$0")" && pwd)
TILES_ROOT=${TILES_ROOT:-$PKG/tiles}
N=${1:?usage: run_n_tiles.sh <n_tiles> [start_tile]}
START=${2:-0}

pass=0; fail=0
for ((t=START; t<START+N; ++t)); do
  tb=$(printf "%s/tile_%04d" "$TILES_ROOT" "$t")
  if [[ ! -f "$tb/segments/segments.json" ]]; then
    echo "[tile $t] missing bundle: $tb" >&2
    exit 1
  fi
  rm -rf "$PKG/case/hbm_data"
  cp -a "$tb/segments" "$PKG/case/hbm_data"
  for s in "$PKG"/case/smc_*; do rm -rf "$s/output_bank"; done
  cp -a "$tb/golden/." "$PKG/case/"
  cp "$tb/meta_v3.json" "$PKG/case/meta_v3.json"
  echo "===== tile $t begin ($(date +%H:%M:%S)) ====="
  if "$PKG/run_one_case.sh" > "$PKG/logs_tile_$(printf %04d "$t").txt" 2>&1; then
    pass=$((pass+1)); echo "[tile $t] PASS"
  else
    fail=$((fail+1)); echo "[tile $t] FAIL (log: $PKG/logs_tile_$(printf %04d "$t").txt)"
  fi
done
echo "TILE LOOP DONE pass=$pass fail=$fail range=[$START,$((START+N)))"
[ "$fail" -eq 0 ]
