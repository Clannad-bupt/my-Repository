#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_celu_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')
    alpha = 1.0
    
    # CELU(x) = max(0, x) + min(0, alpha * (exp(x/alpha) - 1))
    # Taylor for exp(x) - 1: x + x²/2 + x³/6 + x⁴/24 + x⁵/120
    exp_m1 = x + x**2 * 0.5 + x**3 * (1.0/6.0) + x**4 * (1.0/24.0) + x**5 * (1.0/120.0)
    
    # Accurate implementation using Max/Min to handle conditional
    pos_part = sp.Max(0.0, x)
    neg_part = sp.Min(0.0, alpha * exp_m1)
    
    formula = pos_part + neg_part
    
    inputs_meta = {
        x: {"manual_bank": 0, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_celu():
    print("🚀 [Development] celu Activation - Hybrid Max/Min Formula")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )

    # Use range [-0.5, 0.5] for Taylor accuracy
    fx = np.array([-0.5, -0.1, 0.1, 0.5], dtype=np.float32)
    input_data = {"x": fx}
    
    # Golden calculation
    pos_v = np.maximum(0.0, fx)
    exp_m1_v = fx + 0.5*fx**2 + (1/6)*fx**3 + (1/24)*fx**4 + (1/120)*fx**5
    neg_v = np.minimum(0.0, 1.0 * exp_m1_v)
    golden_y = pos_v + neg_v

    success = runner.run_full_pipeline(
        op_name="celu",
        get_dfg_fn=get_celu_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    
    if success:
        print("\n✅ celu PASSED")
    else:
        print("\n❌ celu FAILED")
        sys.exit(1)

if __name__ == "__main__":
    test_celu()
