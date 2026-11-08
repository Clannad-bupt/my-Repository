# tests/operators/op_fuzz.py
import sys
import argparse
import sympy as sp
from pathlib import Path

# 环境路径注入
ws = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ws / "src"))
sys.path.insert(0, str(ws / "tests" / "operators"))

from unified_operator_tester import UnifiedOperatorTester
from utils.op_library import get_op_def

def run_op_fuzz():
    """
    🛸 [ADPC V4.0] 单算子 Bank 碰撞压力测试工具 (Operator Level Fuzzer)
    旨在解决 70+ 算子开发中的“物理拓扑黑盒”问题。
    使用方式: python3 tests/operators/op_fuzz.py reciprocal
    """
    parser = argparse.ArgumentParser(description="AIACC Operator-Level Bank Fuzzing Tool")
    parser.add_argument("op_name", type=str, help="Operator Name to test (e.g. reciprocal, softsign, add)")
    parser.add_argument("--fuzz", action="store_true", default=True, help="Run all bank conflict permutations")
    parser.add_argument("--category", type=str, default="fuzz_diagnostics", help="Output category path")
    args = parser.parse_args()

    try:
        op_meta = get_op_def(args.op_name)
    except ValueError as e:
        print(f"❌ Error: {str(e)}")
        sys.exit(1)

    print(f"\n🔍 [OP-FUZZ] Preparing Stress Test for: {args.op_name}")
    
    # 构造 Symbol
    symbols = [sp.Symbol(s) for s in op_meta["symbols"]]
    
    # 实例化全闭环测试工厂
    tester = UnifiedOperatorTester()
    
    # 触发全布局验证
    success = tester.run_operator_test(
        op_name=args.op_name,
        category=args.category,
        formula_fn=op_meta["formula"],
        golden_fn=op_meta["golden_fn"],
        symbols=symbols,
        input_range=op_meta.get("input_range", (-1, 1)),
        run_fuzz=args.fuzz
    )

    if success:
        print(f"\n✅ [STABLE] Operator '{args.op_name}' successfully passed ALL bank layouts!")
    else:
        print(f"\n❌ [CRITICAL] Operator '{args.op_name}' FAILED in some bank topologies. Check build/operators/{args.category}/{args.op_name}")

if __name__ == "__main__":
    run_op_fuzz()
