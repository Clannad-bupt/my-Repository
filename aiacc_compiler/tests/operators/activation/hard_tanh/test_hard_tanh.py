#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_hard_tanh_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')
    
    # hard_tanh(x, min=-1, max=1) = max(-1, min(1, x))
    formula = sp.Max(-1.0, sp.Min(1.0, x))
    
    inputs_meta = {
        x: {"manual_bank": 0, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_hard_tanh():
    print("🚀 [Development] hard_tanh (max(-1, min(1, x)))")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )

    # Range covering all segments: [-2.0, 2.0]
    fx = np.array([-1.5, -0.5, 0.5, 1.5], dtype=np.float32)
    input_data = {"x": fx}
    
    # Golden calculation
    golden_y = np.maximum(-1.0, np.minimum(1.0, fx))

    success = runner.run_full_pipeline(
        op_name="hard_tanh",
        get_dfg_fn=get_hard_tanh_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    
    if success:
        print("\n✅ hard_tanh PASSED")
    else:
        print("\n❌ hard_tanh FAILED")
        sys.exit(1)

if __name__ == "__main__":
    test_hard_tanh()
