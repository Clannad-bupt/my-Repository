#!/usr/bin/env python3
import sys
import numpy as np
from pathlib import Path
import sympy as sp

ws = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler

def get_log_sigmoid_dfg():
    compiler = GraphCompiler()
    x = sp.Symbol('x')
    
    # log_sigmoid(x) = -log(1 + exp(-x))
    # Approximation for small x:
    # 1. z = -x
    z = -x
    
    # 2. exp(z) approx 1 + z + z²/2 + z³/6 + z⁴/24 + z⁵/120
    exp_nz = 1.0 + z * (1.0 + z * (0.5 + z * (0.1666666667 + z * (0.0416666667 + z * 0.0083333333))))
    
    # 3. w = 1 + exp(-x)
    w = 1.0 + exp_nz
    
    # 4. log(w) = log(2 * (w/2)) = log(2) + log(1 + (w/2 - 1))
    # log(1+u) = u - u²/2 + u³/3 - u⁴/4 + u⁵/5
    # u = (w * 0.5) - 1.0
    u = (w * 0.5) - 1.0
    log_w = 0.69314718 + u * (1.0 - u * (0.5 - u * (0.3333333333 - u * (0.25 - u * 0.2))))
    
    formula = -log_w
    
    inputs_meta = {
        x: {"manual_bank": 3, "role": "operand_a"}
    }
    return compiler.compile(inputs_meta, formula, out_name="y")

def test_log_sigmoid():
    print("🚀 [Development] log_sigmoid (-log(1+exp(-x))) - Taylor Approximation")

    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root="build/operators/activation"
    )

    # Use range [-0.5, 0.5] for Taylor accuracy
    fx = np.array([-0.5, -0.2, 0.2, 0.5], dtype=np.float32)
    input_data = {"x": fx}
    
    # Golden matching the DFG approximation
    nz = -fx
    exp_v = 1.0 + nz + 0.5*nz**2 + (1/6)*nz**3 + (1/24)*nz**4 + (1/120)*nz**5
    w_v = 1.0 + exp_v
    u_v = (w_v * 0.5) - 1.0
    log_w_v = 0.69314718 + (u_v - 0.5*u_v**2 + (1/3)*u_v**3 - 0.25*u_v**4 + 0.2*u_v**5)
    golden_y = -log_w_v

    success = runner.run_full_pipeline(
        op_name="log_sigmoid",
        get_dfg_fn=get_log_sigmoid_dfg,
        input_data=input_data,
        golden_y=golden_y
    )
    
    if success:
        print("\n✅ log_sigmoid PASSED")
    else:
        print("\n❌ log_sigmoid FAILED")
        sys.exit(1)

if __name__ == "__main__":
    test_log_sigmoid()
