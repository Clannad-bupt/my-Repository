#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   RTL_DIR=/path/to/rtl ./run_rtl_test_v3.sh <mul|bitwise_and|remainder_scalar|sub> [--seed N] [--no-rebuild]
#
# Flow:
#   1. build the L-C-S case generator
#   2. generate IQ, HBM init data, golden and meta_v3.json
#   3. generate RTL data_list
#   4. run RTL simulation
#   5. verify DDR/HBM writeback against golden and numpy reference

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SMI_IQ_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

RTL_DIR="${RTL_DIR:-/home/clannad/Jianyuan_release_v4.19}"

OPERATOR=""
SEED="$(date +%s)"
REBUILD_FLAG="--rebuild"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --seed) SEED="$2"; shift 2;;
        --no-rebuild) REBUILD_FLAG=""; shift;;
        -*) echo "Unknown option: $1"; exit 1;;
        *) OPERATOR="$1"; shift;;
    esac
done

[[ -z "$OPERATOR" ]] && {
    echo "Usage: [RTL_DIR=...] $0 <mul|bitwise_and|remainder_scalar|sub> [--seed N] [--no-rebuild]"
    exit 1
}
[[ -d "$RTL_DIR" ]] || { echo "ERROR: RTL_DIR does not exist: $RTL_DIR"; exit 1; }

CASE_NAME="${OPERATOR}_8smc_v3"
CASE_REL="verification_v2/${CASE_NAME}"
CASE_ABS="${RTL_DIR}/test_atomic_ops/${CASE_REL}"
OUT_DIR="${SCRIPT_DIR}/out"
GEN_BIN="${OUT_DIR}/gen_${OPERATOR}_lsu_case"
mkdir -p "$OUT_DIR"

echo "=============================================="
echo " load_store_v3 RTL test (verification_v2 bulk mode)"
echo " Operator: $OPERATOR   Seed: $SEED"
echo " RTL:      $RTL_DIR"
echo " Case:     $CASE_REL"
[[ -n "${LSU_DTYPE:-}" ]] && echo " DType:    ${LSU_DTYPE}"
[[ -n "${LLAMA_SUB_DIR:-}" ]] && echo " LLAMA:    ${LLAMA_SUB_DIR} word_offset=${LLAMA_WORD_OFFSET:-0} wc=${LSU_WC:-256}"
echo "=============================================="

echo ""
echo "[1/5] Build $OPERATOR L-C-S generator..."
LSU_MAIN=$(find "$SCRIPT_DIR" -path "*/${OPERATOR}/*" -name "gen_*_lsu_case.cpp" | head -1)
[[ -z "$LSU_MAIN" ]] && LSU_MAIN=$(find "$SCRIPT_DIR" -maxdepth 1 -name "gen_${OPERATOR}_lsu_case.cpp" | head -1)
LSU_OP_SRC=$(find "$SCRIPT_DIR" -path "*/${OPERATOR}/*" -name "*_lsu_op.cpp" | head -1)
if [[ -z "$LSU_MAIN" || -z "$LSU_OP_SRC" ]]; then
    echo "ERROR: missing ${OPERATOR}/gen_*_lsu_case.cpp or *_lsu_op.cpp"
    exit 1
fi

cd "$SMI_IQ_DIR"
if [[ "$OPERATOR" == "sub" ]]; then
    OP_SRCS=$(find op/common op/sub -name "*.cpp" \
        | grep -vE "/test_" \
        | grep -vE "_main\.cpp$" \
        | tr '\n' ' ')
    APP_SRCS="app/fp16_utils.cpp"
else
    OP_SRCS=$(find op -name "*.cpp" \
        | grep -vE "/(div|div_scalar|select|expand|Mean-dim|mul_scalar|softmax|sub|add|cos|exp|neg|pow|unsqueeze)/" \
        | grep -vE "/test_" \
        | grep -vE "_main\.cpp$" \
        | tr '\n' ' ')
    APP_SRCS="app/tvgen_core.cpp app/tvgen_dynamic.cpp app/simulator_config_gen.cpp app/app_json_parser.cpp app/fp16_utils.cpp math_utils/newton_method/newton_method.cpp math_utils/taylor_series/taylor_series.cpp"
fi
IQ_SRCS=$(find iq -name "*.cpp" | tr '\n' ' ')

BUILD_LOG="${OUT_DIR}/build_${OPERATOR}.log"
if ! g++ -std=c++17 -O2 -fno-stack-protector -I"$SMI_IQ_DIR" -I"$SMI_IQ_DIR/app" \
    "$LSU_MAIN" "$SCRIPT_DIR/lsu_op_base.cpp" "$LSU_OP_SRC" \
    $OP_SRCS $IQ_SRCS \
    $APP_SRCS \
    -o "$GEN_BIN" > "$BUILD_LOG" 2>&1; then
    echo "ERROR: generator build failed. See $BUILD_LOG"
    tail -80 "$BUILD_LOG" || true
    exit 1
fi
[[ -x "$GEN_BIN" ]] || { echo "ERROR: generator binary was not produced"; exit 1; }

echo ""
echo "[2/5] Generate case (seed=$SEED)..."
rm -rf "$CASE_ABS"
"$GEN_BIN" "$CASE_ABS" "${RTL_DIR}/data_engine/sram" "$SEED"

echo ""
echo "[3/5] Generate data_list..."
cd "$RTL_DIR"
python3 ./generate_data_list_coverage.py "$CASE_REL" --subq 0 > /dev/null 2>&1

echo ""
echo "[4/5] Run RTL..."
LOG="${OUT_DIR}/${CASE_NAME}_rtl.log"
SKIP_COMPARE=0 bash ./run_sim_aiacc_top_f.sh $REBUILD_FLAG verification_v2 "${CASE_REL}/smc_0" -s 0 > "$LOG" 2>&1 || true
echo -n "  UR compare summary: "
grep "Summary" "$LOG" | tail -1 || echo "(no Summary)"

echo ""
echo "[5/5] DDR verify..."
python3 "${SCRIPT_DIR}/verify_ddr_v3.py" "$CASE_ABS"
VERIFY_RC=$?

echo ""
echo "=============================================="
echo "  DDR verify rc: ${VERIFY_RC} (0=PASS)"
FSDB="$(find sim_output_aiacc_top_f -name '*.fsdb' 2>/dev/null | head -1)"
[[ -n "$FSDB" ]] && echo "  FSDB: ${RTL_DIR}/${FSDB}"
echo "  Log: $LOG"
echo "=============================================="
exit $VERIFY_RC
