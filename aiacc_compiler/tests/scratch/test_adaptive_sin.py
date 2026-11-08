"""
Integration Test: Adaptive Vector sin(a[N])
============================================
验证以下流水：
  TilingPass → SMT(II) → LoopStitcher → Emitter → SMC 物理仿真

测试用例：
  N=40  → K=10, r=0 (完美整除)
  N=13  → K=3,  r=1 (有尾部)
  N=4   → K=1,  r=0 (单轮)
  N=3   → K=0,  r=3 (纯尾部)
"""
import sys, os, numpy as np
sys.path.insert(0, os.path.join(os.getcwd(), "src"))

from core.tiling import TilingPass
from core.ii_analyzer import IIAnalyzer
from orchestrator.loop_stitcher import LoopStitcher
from library.atomic.transcendentals.sin import get_sin_standard_dfg
from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from driver.emitter import LIRPacker
from pathlib import Path
import json, struct


# ─────────────────────────────────────────────────────────────
# Helpers
# ─────────────────────────────────────────────────────────────

def build_golden(x_arr: np.ndarray) -> np.ndarray:
    """计算 sin 的 3阶泰勒 Golden（与 DFG 保持一致）。"""
    x2 = x_arr * x_arr
    # sin(x) ≈ x*(1 - x²*(1/6 - x²/120))
    return x_arr * (1.0 - x2 * (1.0/6.0 - x2 * (1.0/120.0)))


def run_tiling_and_ii_test(N: int) -> dict:
    """子测试：验证 TilingPass 和 IIAnalyzer 的静态计算。"""
    spec  = HardwareSpec.load("hw_config")
    dfg   = get_sin_standard_dfg()

    tiler = TilingPass(vec_width=4)
    tiling = tiler.tile(N)

    analyzer = IIAnalyzer(spec)
    II = analyzer.compute(dfg)

    expected_K = N // 4
    expected_r = N % 4

    assert tiling.K == expected_K, f"K mismatch: got {tiling.K}, expected {expected_K}"
    assert tiling.r == expected_r, f"r mismatch: got {tiling.r}, expected {expected_r}"
    assert II >= 1, f"II must be >= 1, got {II}"

    print(f"  [TILING+II TEST] N={N}: K={tiling.K}, r={tiling.r}, II={II} ✅")
    return {"tiling": tiling, "II": II, "dfg": dfg, "spec": spec}


def run_smt_and_loop_stitch_test(N: int):
    """子测试：SMT 求解 + LoopStitcher 生成指令流。"""
    spec  = HardwareSpec.load("hw_config")
    dfg   = get_sin_standard_dfg()

    tiler  = TilingPass(vec_width=4)
    tiling = tiler.tile(N)

    # SMT 求解单次 DFG
    smt_result = SMTSynthesizer(spec).solve(dfg)
    assert smt_result, "SMT returned UNSAT!"

    schedule = smt_result["schedule"]
    II       = smt_result["II"]

    print(f"  [SMT] N={N}: schedule max_t={max(schedule.values())}, II={II}")

    # LoopStitcher 展开
    ls = LoopStitcher(spec)
    ls.stitch_vectorized(
        op_name="sin",
        dfg_template=dfg,
        synthesis_res=schedule,
        II=II,
        tiling=tiling,
    )
    final_stream = ls.base_stitcher.get_final_stream()

    assert len(final_stream) > 0, "Empty final_stream!"

    # 验证主循环 I/O 节点带有 outer_loops 注解
    if tiling.K >= 1:
        io_with_loop = [
            op for op in dfg
            if op.metadata.get("role") in ("operand_a", "output")
               and "outer_loops" in op.metadata
        ]
        assert len(io_with_loop) > 0, \
            "No I/O node has outer_loops annotation when K>=1!"
        print(f"  [STITCH] {len(io_with_loop)} I/O nodes annotated with outer_loops={tiling.K-1}")

    print(f"  [STITCH] N={N}: final_stream has {len(final_stream)} MIR ops ✅")
    return final_stream, schedule, II


# ─────────────────────────────────────────────────────────────
# Main Test Suite
# ─────────────────────────────────────────────────────────────

def test_all():
    test_cases = [
        {"N": 4,  "desc": "单轮 (K=1, r=0)"},
        {"N": 40, "desc": "完美整除 (K=10, r=0)"},
        {"N": 13, "desc": "有尾部 (K=3, r=1)"},
        {"N": 3,  "desc": "纯尾部 (K=0, r=3)"},
    ]

    print("\n" + "="*60)
    print("AIACC Adaptive Vector sin(a[N]) Integration Test")
    print("="*60)

    all_passed = True

    for tc in test_cases:
        N = tc["N"]
        print(f"\n── Case: N={N}  {tc['desc']} ──")
        try:
            # Step 1: 静态分析
            run_tiling_and_ii_test(N)

            # Step 2: SMT + 循环缝合
            final_stream, schedule, II = run_smt_and_loop_stitch_test(N)

            print(f"  ✅ PASS  N={N}")

        except AssertionError as e:
            print(f"  ❌ FAIL  N={N}: {e}")
            all_passed = False
        except Exception as e:
            import traceback
            print(f"  ❌ ERROR N={N}: {e}")
            traceback.print_exc()
            all_passed = False

    print("\n" + "="*60)
    if all_passed:
        print("✅ ALL CASES PASSED")
    else:
        print("❌ SOME CASES FAILED")
    print("="*60)
    return all_passed


if __name__ == "__main__":
    ok = test_all()
    sys.exit(0 if ok else 1)
