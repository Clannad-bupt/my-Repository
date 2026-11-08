#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_fmod_dfg():
    compiler = GraphCompiler()
    a = sp.Symbol('a')
    b = sp.Symbol('b')
    
    # fmod(a, b) = a - b * trunc(a / b)
    # 1. Taylor Reciprocal (Order 5)
    dx = b - 1.0
    recip = 1.0
    for _ in range(5):
        recip = 1.0 - dx * recip
        
    # 2. a / b
    q = a * recip
    
    # 3. fmod = a - b * floor(q) (Assuming positive inputs range for validation)
    # Using sp.floor to test the LOGIC/ST port constraint
    formula = a - b * sp.floor(q)
    
    inputs_meta = {
        a: {"role": "operand_a"},
        b: {"role": "operand_b"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_fmod():
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/binary/math"
    )

    fa = np.array([2.5, 3.1, 4.2, 5.0], dtype=np.float32)
    fb = np.array([1.1, 1.2, 1.1, 2.0], dtype=np.float32)
    input_data = {"a": fa, "b": fb}
    
    # Golden calculation
    dx_v = fb - 1.0
    recip_v = 1.0
    for _ in range(5): recip_v = 1.0 - dx_v * recip_v
    q_v = fa * recip_v
    golden_y = fa - fb * np.floor(q_v)

    success = runner.run_full_pipeline(
        op_name="fmod",
        get_dfg_fn=get_fmod_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    test_fmod()
