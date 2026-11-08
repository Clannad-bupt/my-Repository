#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def run_logical_test(op_name, formula_fn, golden_fn):
    runner = StandardPipelineRunner(hw_config="hw_config", build_root="build/operators/binary/logical")
    a = sp.Symbol('a')
    b = sp.Symbol('b')
    compiler = GraphCompiler()
    
    def get_dfg():
        return compiler.compile({a: {"role": "operand_a"}, b: {"role": "operand_b"}}, formula_fn(a, b), out_name="y")

    a_dat = np.array([1.5, 2.0, -1.0, 5.0], dtype=np.float32)
    b_dat = np.array([2.0, 1.5, -2.0, 5.0], dtype=np.float32)
    golden_y = golden_fn(a_dat, b_dat).astype(np.float32)

    success = runner.run_full_pipeline(op_name=op_name, get_dfg_fn=get_dfg, input_data={"a": a_dat, "b": b_dat}, golden_y=golden_y)
    return success

if __name__ == "__main__":
    op = sys.argv[1]
    if op == "gt":
        res = run_logical_test("gt", lambda a, b: sp.Gt(a, b), lambda a, b: a > b)
    elif op == "ge":
        res = run_logical_test("ge", lambda a, b: sp.Ge(a, b), lambda a, b: a >= b)
    elif op == "lt":
        res = run_logical_test("lt", lambda a, b: sp.Lt(a, b), lambda a, b: a < b)
    elif op == "le":
        res = run_logical_test("le", lambda a, b: sp.Le(a, b), lambda a, b: a <= b)
    elif op == "eq":
        res = run_logical_test("eq", lambda a, b: sp.Eq(a, b), lambda a, b: a == b)
    elif op == "ne":
        res = run_logical_test("ne", lambda a, b: sp.Ne(a, b), lambda a, b: a != b)
    elif op == "and":
        res = run_logical_test("and", lambda a, b: a & b, 
                               lambda a, b: np.frombuffer((a.view(np.int32) & b.view(np.int32)), dtype=np.float32))
    elif op == "or":
        res = run_logical_test("or", lambda a, b: a | b, 
                               lambda a, b: np.frombuffer((a.view(np.int32) | b.view(np.int32)), dtype=np.float32))
    elif op == "xor":
        res = run_logical_test("xor", lambda a, b: a ^ b, 
                               lambda a, b: np.frombuffer((a.view(np.int32) ^ b.view(np.int32)), dtype=np.float32))
    elif op == "not":
        res = run_logical_test("not", lambda a, b: sp.Not(a), lambda a, b: (a == 0.0).astype(np.float32))
    else:
        print("Unknown op")
        sys.exit(1)
    sys.exit(0 if res else 1)
