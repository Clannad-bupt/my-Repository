#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LSU_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
    cat <<'USAGE'
Usage:
  run_llama_sub_chunks.sh <aten_sub_dump_dir> [options]

Options:
  --rtl-dir DIR       RTL repository root passed as RTL_DIR
  --chunk-words N    Words per chunk, max 256; 256 is emitted as two 128-word LDB/STB transfers (default: 256)
  --start-word N     First 16-byte word offset in self/other/return bins (default: 0)
  --num-words N      Total words to cover from start-word (default: whole tensor)
  --seed N           Seed for non-LLAMA fields (default: 12345)
  --no-rebuild       Skip RTL rebuild for the first chunk too

The dump directory must contain:
  *-self.bin
  *-other.bin
  *-return.bin

Each chunk sets:
  LLAMA_SUB_DIR=<dump_dir>
  LLAMA_WORD_OFFSET=<word offset>
  LSU_DTYPE=fp16
  LSU_WC=<chunk words>
  SUB_ALPHA_ONE=1
  SUB_FULL_DDR=${SUB_FULL_DDR:-1}
  SUB_SMC_WORD_STRIDE=<chunk words>
  SUB_NUM_SMC=<active SMC count>
  SUB_USE_LLAMA_RETURN_GOLDEN=${SUB_USE_LLAMA_RETURN_GOLDEN:-1}
USAGE
}

if [[ $# -lt 1 ]]; then
    usage
    exit 2
fi

DUMP_DIR="$1"
shift

RTL_DIR_ARG=""
CHUNK_WORDS=256
START_WORD=0
NUM_WORDS=""
SEED=12345
FIRST_NO_REBUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rtl-dir) RTL_DIR_ARG="$2"; shift 2;;
        --chunk-words) CHUNK_WORDS="$2"; shift 2;;
        --start-word) START_WORD="$2"; shift 2;;
        --num-words) NUM_WORDS="$2"; shift 2;;
        --seed) SEED="$2"; shift 2;;
        --no-rebuild) FIRST_NO_REBUILD=1; shift;;
        -h|--help) usage; exit 0;;
        *) echo "Unknown option: $1"; usage; exit 2;;
    esac
done

if [[ ! -d "$DUMP_DIR" ]]; then
    echo "ERROR: dump dir does not exist: $DUMP_DIR"
    exit 2
fi
if (( CHUNK_WORDS < 1 || CHUNK_WORDS > 256 )); then
    echo "ERROR: --chunk-words must be in [1,256]; 256 is emitted as two 128-word transfers"
    exit 2
fi

SELF_BIN="$(find "$DUMP_DIR" -maxdepth 1 -name '*-self.bin' | head -1)"
OTHER_BIN="$(find "$DUMP_DIR" -maxdepth 1 -name '*-other.bin' | head -1)"
RETURN_BIN="$(find "$DUMP_DIR" -maxdepth 1 -name '*-return.bin' | head -1)"
if [[ -z "$SELF_BIN" || -z "$OTHER_BIN" || -z "$RETURN_BIN" ]]; then
    echo "ERROR: dump dir must contain *-self.bin, *-other.bin, and *-return.bin"
    exit 2
fi

SELF_BYTES="$(stat -c%s "$SELF_BIN")"
TOTAL_WORDS=$(( SELF_BYTES / 16 ))
if [[ -z "$NUM_WORDS" ]]; then
    NUM_WORDS=$(( TOTAL_WORDS - START_WORD ))
fi
END_WORD=$(( START_WORD + NUM_WORDS ))
if (( END_WORD > TOTAL_WORDS )); then
    echo "ERROR: requested range exceeds tensor words: end=$END_WORD total=$TOTAL_WORDS"
    exit 2
fi

echo "LLAMA sub chunk verification"
echo "  dump:        $DUMP_DIR"
echo "  total words: $TOTAL_WORDS"
echo "  range:       [$START_WORD, $END_WORD)"
echo "  chunk words: $CHUNK_WORDS"
echo "  full DDR:    ${SUB_FULL_DDR:-1}"
echo "  golden:      return.bin"

burst_safe() {
    local off="$1"
    local words="$2"
    local pos=$(( off % 256 ))
    local rem="$words"
    local page_rem
    local beats
    while (( rem > 0 )); do
        page_rem=$(( 256 - pos ))
        beats="$rem"
        if (( beats > 16 )); then beats=16; fi
        if (( beats > page_rem )); then beats="$page_rem"; fi
        if (( beats == 1 )); then
            return 1
        fi
        rem=$(( rem - beats ))
        pos=$(( (pos + beats) % 256 ))
    done
    return 0
}

choose_wc() {
    local off="$1"
    local max_words="$2"
    local wc
    for (( wc=max_words; wc>=2; wc-- )); do
        if burst_safe "$off" "$wc"; then
            echo "$wc"
            return 0
        fi
    done
    return 1
}

offset=$START_WORD
chunk_id=0
while (( offset < END_WORD )); do
    run_offset=$offset
    if (( run_offset % 256 == 255 && run_offset > 0 )); then
        run_offset=$(( run_offset - 1 ))
    fi

    remain=$(( END_WORD - run_offset ))
    available=$(( TOTAL_WORDS - run_offset ))
    max_wc=$CHUNK_WORDS
    if (( available < max_wc )); then max_wc=$available; fi
    if (( remain >= 2 && remain < max_wc )); then max_wc=$remain; fi
    if (( remain == 1 && max_wc > 2 )); then max_wc=2; fi

    if ! wc="$(choose_wc "$run_offset" "$max_wc")"; then
        if (( run_offset > 0 )); then
            run_offset=$(( run_offset - 1 ))
            remain=$(( END_WORD - run_offset ))
            available=$(( TOTAL_WORDS - run_offset ))
            max_wc=$CHUNK_WORDS
            if (( available < max_wc )); then max_wc=$available; fi
            if (( remain >= 2 && remain < max_wc )); then max_wc=$remain; fi
            if (( remain == 1 && max_wc > 2 )); then max_wc=2; fi
            if ! wc="$(choose_wc "$run_offset" "$max_wc")"; then
                echo "ERROR: cannot form a page-safe chunk at word_offset=$offset words_left=$remain"
                echo "       Increase --chunk-words to at least 2 or widen the requested range by one word."
                exit 2
            fi
        else
            echo "ERROR: cannot form a page-safe chunk at word_offset=$offset words_left=$remain"
            echo "       The RTL path cannot run 1-beat bursts; request at least 2 words or start from a previous word."
            exit 2
        fi
    fi

    active_smc=$(( remain / wc ))
    if (( active_smc < 1 )); then active_smc=1; fi
    if (( active_smc > 8 )); then active_smc=8; fi

    echo ""
    echo "========== chunk ${chunk_id}: word_offset=${run_offset}, words=${wc}, active_smc=${active_smc}, wave_words=$(( wc * active_smc )) =========="
    for (( smc=0; smc<active_smc; smc++ )); do
        smc_begin=$(( run_offset + smc * wc ))
        smc_end=$(( smc_begin + wc ))
        echo "  smc_${smc}: words [${smc_begin}, ${smc_end})"
    done
    if (( run_offset < offset )); then
        echo "  note: overlaps previous/requested range by $(( offset - run_offset )) word to avoid a 1-beat burst"
    fi

    args=(sub --seed "$SEED")
    if (( chunk_id > 0 || FIRST_NO_REBUILD == 1 )); then
        args+=(--no-rebuild)
    fi

    if [[ -n "$RTL_DIR_ARG" ]]; then
        RTL_DIR="$RTL_DIR_ARG" \
        LLAMA_SUB_DIR="$DUMP_DIR" \
        LLAMA_WORD_OFFSET="$run_offset" \
        LSU_DTYPE=fp16 \
        LSU_WC="$wc" \
        SUB_SMC_WORD_STRIDE="$wc" \
        SUB_NUM_SMC="$active_smc" \
        SUB_SMC_ID_EN="$(( active_smc - 1 ))" \
        SUB_ALPHA_ONE=1 \
        SUB_FULL_DDR="${SUB_FULL_DDR:-1}" \
        SUB_TOTAL_WORDS="$TOTAL_WORDS" \
        SUB_USE_LLAMA_RETURN_GOLDEN="${SUB_USE_LLAMA_RETURN_GOLDEN:-1}" \
        "${LSU_DIR}/run_rtl_test_v3.sh" "${args[@]}"
    else
        LLAMA_SUB_DIR="$DUMP_DIR" \
        LLAMA_WORD_OFFSET="$run_offset" \
        LSU_DTYPE=fp16 \
        LSU_WC="$wc" \
        SUB_SMC_WORD_STRIDE="$wc" \
        SUB_NUM_SMC="$active_smc" \
        SUB_SMC_ID_EN="$(( active_smc - 1 ))" \
        SUB_ALPHA_ONE=1 \
        SUB_FULL_DDR="${SUB_FULL_DDR:-1}" \
        SUB_TOTAL_WORDS="$TOTAL_WORDS" \
        SUB_USE_LLAMA_RETURN_GOLDEN="${SUB_USE_LLAMA_RETURN_GOLDEN:-1}" \
        "${LSU_DIR}/run_rtl_test_v3.sh" "${args[@]}"
    fi

    offset=$(( run_offset + wc * active_smc ))
    chunk_id=$(( chunk_id + 1 ))
done

echo ""
echo "PASS: all requested LLAMA sub chunks passed"
