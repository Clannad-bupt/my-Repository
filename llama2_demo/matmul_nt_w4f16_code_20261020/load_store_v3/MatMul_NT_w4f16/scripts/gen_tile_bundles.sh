#!/usr/bin/env bash
# 阶梯 6c 任务 B：4 个形状全部 N-tile bundle（image-only 生成 + 槽位段目录），共 944 个。
# 与任务 A（stage 树）并发安全：只写 cases/*_img_t* 与 bundles/，不碰 stage 树。
set -uo pipefail

WK=/home/wenyang.zhang/aiacc/ipset/1_ip/GPLPU/v1.0/3_tool/tvgen/load_store_v3/MatMul_NT_w4f16/fpga_workspace
PY="$WK/scripts/mk_tile_bundle.py"

loop_shape() { # $1=name $2=M $3=K $4=N $5=tdir $6=ntiles
  local name=$1 ntiles=$6 ok=0
  echo "[tiles] $name start ($ntiles tiles, $(date +%H:%M:%S))"
  for ((t=0; t<ntiles; ++t)); do
    if python3 "$PY" "$1" "$2" "$3" "$4" "$5" "$t" >>"$WK/logs/tiles_$1.log" 2>&1; then
      ok=$((ok+1))
    else
      echo "[tiles] $name tile $t FAILED (see logs/tiles_$1.log)"; return 1
    fi
  done
  echo "[tiles] $name done: $ok/$ntiles ($(date +%H:%M:%S))"
  [ "$ok" -eq "$ntiles" ]
}

loop_shape gate_up_dec 1 4096 11008 m1_k4096_n11008 344 || exit 1
loop_shape down_dec   1 11008 4096 m1_k11008_n4096 128 || exit 1
loop_shape gate_up_pre 256 4096 11008 m256_k4096_n11008 344 || exit 1
loop_shape down_pre   256 11008 4096 m256_k11008_n4096 128 || exit 1
echo ALL_TILE_BUNDLES_DONE
