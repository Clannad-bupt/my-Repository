#!/usr/bin/env bash
# 阶梯 6c 任务 A：4 个形状的 tile-0 全量 stage case（fpga 槽位）。
# 生成真 stage 树（host 端 tile 循环共用同一棵，仅镜像按 tile 换），随后 data_list + 打包 decode 两形状。
set -uo pipefail

TVGEN=/home/wenyang.zhang/aiacc/ipset/1_ip/GPLPU/v1.0/3_tool/tvgen
WK="$TVGEN/load_store_v3/MatMul_NT_w4f16/fpga_workspace"
GEN="$WK/bin/gen_w4f16_check"
TB=/home/wenyang.zhang/aiacc/ipset/1_ip/GPLPU/v1.0/2_verification/aiacc/testbench
SIM="$TB/sim"
DATALIST="$TB/scripts/sim_multi_stage/generate_data_list_coverage_multi_stage.py"
PACKER="$SIM/package_case_to_fpga.py"

gen_one() { # $1=name $2=M $3=K $4=N $5=tdir
  local name=$1 M=$2 K=$3 N=$4 tdir=$5
  local croot="$WK/cases/${name}_stage_t0"
  rm -rf "$croot"; mkdir -p "$croot/hbm_init"
  echo "[stage] $name gen start M=$M K=$K N=$N ($(date +%H:%M:%S))"
  if MM_M=$M MM_OUT_DTYPE=f16 MM_MULTISTAGE=1 MM_GROUP_SIZE=128 \
     MM_N=$N K_TOTAL=$K MM_SLOT_MAP=fpga MM_N_TILE_START=0 \
     MM_TENSOR_DIR="$WK/tensors/$tdir" \
     "$GEN" "$croot" "$croot/hbm_init" 42 > "$WK/logs/stage_${name}.log" 2>&1; then
    echo "[stage] $name gen OK"
  else
    echo "[stage] $name gen FAILED (see logs/stage_${name}.log)"; return 1
  fi
  ln -sfn "$croot" "$SIM/w4f16_fpga_${name}_t0"
  if python3 "$DATALIST" "w4f16_fpga_${name}_t0" > "$WK/logs/datalist_${name}.log" 2>&1; then
    echo "[stage] $name data_list OK"
  else
    echo "[stage] $name data_list FAILED (see logs/datalist_${name}.log)"; return 1
  fi
  return 0
}

pack_decode() { # $1=name
  local name=$1
  python3 "$PACKER" --case "w4f16_fpga_${name}_t0" \
      --out "$WK/pkgs/${name}_t0" --force > "$WK/logs/pack_${name}.log" 2>&1 \
    && echo "[stage] $name packaged OK" \
    || { echo "[stage] $name pack FAILED (see logs/pack_${name}.log)"; return 1; }
}

gen_one gate_up_dec 1 4096 11008 m1_k4096_n11008 || exit 1
pack_decode gate_up_dec || exit 1

gen_one down_dec 1 11008 4096 m1_k11008_n4096 || exit 1
pack_decode down_dec || exit 1

# 交叉校验：image-only 链路的 tile_0000 段必须与全量打包的段逐字节一致（等 bundle 任务落盘，最多等 1h）。
echo "[stage] waiting for gate_up_dec tile_0000 bundle ..."
for i in $(seq 1 360); do
  [ -f "$WK/bundles/gate_up_dec/tile_0000/segments/segments.json" ] && break
  sleep 10
done
if [ -f "$WK/bundles/gate_up_dec/tile_0000/segments/segments.json" ]; then
  bad=0
  for f in a_tensor b_tensor scale_tensor zp_tensor zeros; do
    for s in 0 3 7; do
      cmp -s "$WK/bundles/gate_up_dec/tile_0000/segments/ch${s}_${f}.hex" \
             "$WK/pkgs/gate_up_dec_t0/case/hbm_data/ch${s}_${f}.hex" \
        || { echo "[check] MISMATCH ch${s}_${f}"; bad=1; }
    done
  done
  [ $bad -eq 0 ] && echo "[check] tile0 bundle == full-pack segments (ch 0/3/7, 5 files each): IDENTICAL"
else
  echo "[check] bundle tile_0000 never appeared; skipped cross-check"
fi

gen_one gate_up_pre 256 4096 11008 m256_k4096_n11008 || exit 1
gen_one down_pre   256 11008 4096 m256_k11008_n4096 || exit 1
echo ALL_STAGE_CASES_DONE
