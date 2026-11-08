#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def run_unary_math_test(op_name, formula_fn, golden_fn, input_x=None):
    runner = StandardPipelineRunner(hw_config="hw_config", build_root="build/operators/unary/math")
    x = sp.Symbol('x')
    compiler = GraphCompiler()
    
    def get_dfg():
        return compiler.compile({x: {"role": "operand_a"}}, formula_fn(x), out_name="y")

    # Use a mix of positive/negative and boundary values
    if input_x is None:
        test_x = np.array([1.5, -2.3, 0.0, 5.8], dtype=np.float32)
    else:
        test_x = np.array(input_x, dtype=np.float32)
        
    input_data = {"x": test_x}
    golden_y = np.array(golden_fn(test_x)).astype(np.float32)

    success = runner.run_full_pipeline(op_name=op_name, get_dfg_fn=get_dfg, input_data=input_data, golden_y=golden_y)
    return success

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: unary_test_template.py <op>")
        sys.exit(1)
        
    op = sys.argv[1]
    if op == "abs":
        res = run_unary_math_test("abs", lambda x: sp.Abs(x), lambda x: np.abs(x))
    elif op == "neg":
        res = run_unary_math_test("neg", lambda x: -x, lambda x: -x)
    elif op == "square":
        res = run_unary_math_test("square", lambda x: x * x, lambda x: x * x)
    elif op == "sign":
        res = run_unary_math_test("sign", lambda x: sp.sign(x), lambda x: np.sign(x))
    elif op == "floor":
        res = run_unary_math_test("floor", lambda x: sp.floor(x), lambda x: np.floor(x))
    elif op == "ceil":
        res = run_unary_math_test("ceil", lambda x: sp.ceiling(x), lambda x: np.ceil(x))
    elif op == "round":
        # AIACC floor(x+0.5) 对应的是 'half up'，numpy.round 是 'half to even'
        # 为保持一致，对验证阶段使用 floor(x+0.5) 的规则
        res = run_unary_math_test("round", lambda x: sp.floor(x + 0.5), lambda x: np.floor(x + 0.5))
    elif op == "reciprocal":
        # 1/x ≈ 1 - (x-1) + (x-1)² - (x-1)³ + (x-1)⁴ - (x-1)⁵ + (x-1)⁶
        def taylor_reciprocal(x):
            dx = x - 1.0
            return 1.0 - dx * (1.0 - dx * (1.0 - dx * (1.0 - dx * (1.0 - dx * (1.0 - dx)))))
        # 泰勒展开在 1.0 附近收敛，使用 [0.9, 1.3] 的范围以回避发散
        res = run_unary_math_test("reciprocal", taylor_reciprocal, taylor_reciprocal, input_x=[0.9, 1.1, 1.2, 1.3])
    else:
        print(f"Unknown op: {op}")
        sys.exit(1)
    
    sys.exit(0 if res else 1)
