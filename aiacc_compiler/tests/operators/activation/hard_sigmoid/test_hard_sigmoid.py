#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_hard_sigmoid_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')
    
    # hard_sigmoid(x) = max(0, min(1, 0.166666667 * x + 0.5))
    formula = sp.Max(0.0, sp.Min(1.0, 0.166666667 * x + 0.5))
    
    inputs_meta = {
        x: {"manual_bank": 1, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_hard_sigmoid():
    print("🚀 [Development] hard_sigmoid (max(0, min(1, x/6 + 0.5)))")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )

    # Range should cover clipping regions: [-4.0, 4.0]
    fx = np.array([-4.0, -2.0, 2.0, 4.0], dtype=np.float32)
    input_data = {"x": fx}
    
    # Golden calculation
    inner = 0.166666667 * fx + 0.5
    golden_y = np.maximum(0.0, np.minimum(1.0, inner))

    success = runner.run_full_pipeline(
        op_name="hard_sigmoid",
        get_dfg_fn=get_hard_sigmoid_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    
    if success:
        print("\n✅ hard_sigmoid PASSED")
    else:
        print("\n❌ hard_sigmoid FAILED")
        sys.exit(1)

if __name__ == "__main__":
    test_hard_sigmoid()
