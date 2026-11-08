# tests/operators/run_factory_ops.py
import sys
import os
import sympy as sp
import numpy as np
from pathlib import Path

# 环境路径注入
ws = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ws / "src"))
sys.path.insert(0, str(ws / "tests" / "operators"))

from unified_operator_tester import UnifiedOperatorTester

def run_stabilized_operators():
    tester = UnifiedOperatorTester()
    x = sp.Symbol('x')
    
    # 🧪 [1] Reciprocal (1/x) - 16阶 Taylor-Linear
    print("\n✅ Validating: Reciprocal (1/x)")
    tester.run_operator_test(
        op_name="reciprocal",
        category="unary/math",
        formula_fn=lambda syms: 1.0 / syms[0], # 编译器内部自动处理 Taylor-16
        golden_fn=lambda data: 1.0 / data['x'],
        symbols=[x],
        input_range=(0.5, 1.5),
        run_fuzz=True # 探测 2 种布局: [0, 1] - 隔离, [0, 0] - 冲突
    )

    # 🧪 [2] Softsign (x / (1 + |x|))
    print("\n✅ Validating: Softsign (x / (1 + |x|))")
    tester.run_operator_test(
        op_name="softsign",
        category="activation",
        formula_fn=lambda syms: syms[0] / (1.0 + sp.Abs(syms[0])),
        golden_fn=lambda data: data['x'] / (1.0 + np.abs(data['x'])),
        symbols=[x],
        input_range=(-1.0, 1.0),
        run_fuzz=True
    )

if __name__ == "__main__":
    run_stabilized_operators()
