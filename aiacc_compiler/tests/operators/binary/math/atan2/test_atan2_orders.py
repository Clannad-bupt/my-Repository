#!/usr/bin/env python3
"""
atan2(y, x) 逐步升阶 SMT 压力测试

旨在寻找 AIACC SMT 求解器的临界资源占用点。
1阶 (Linear) -> 3阶 (Basic) -> 5阶 (Advanced) -> 7阶 (Extreme)
"""
import sys
import numpy as np
from pathlib import Path
import sympy as sp
import time

ws = Path(__file__).resolve().parents[5]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler


def get_atan2_dfg(order=1):
    compiler = GraphCompiler()
    y_sym = sp.Symbol('y_in')
    x_sym = sp.Symbol('x_in')
    
    # Step 1: reciprocal(x)
    dx = x_sym - 1.0
    recip = 1.0
    for _ in range(order):
        recip = 1.0 - dx * recip
        
    # Step 2: z = y/x near 1.0
    z = y_sym * recip
    
    # Step 3: atan(z) ≈ z - z³/3 + z⁵/5
    z2 = z * z
    if order == 1:
        formula = z
    elif order == 3:
        # Avoid SymPy expansion by using a pre-factored form
        # We assign it to a list and pick the first to simulate a 'sequential' block
        term3 = z2 * 0.3333333333
        formula = z * (1.0 - term3)
    elif order >= 5:
        term3 = z2 * 0.3333333333
        term5 = z2 * z2 * 0.2
        formula = z * (1.0 - term3 + term5)
    else:
        formula = z
    
    inputs_meta = {
        y_sym: {"manual_bank": 0, "role": "operand_a"},
        x_sym: {"manual_bank": 1, "role": "operand_b"}
    }
    # 输出名称使用 'out' 以避免与输入 'y' 冲突
    return compiler.compile(inputs_meta, formula, out_name="out")


def run_order_test(order):
    print(f"\n" + "="*40)
    print(f" 🧪 Testing Order: {order}")
    print(f"="*40)
    
    runner = StandardPipelineRunner(
        hw_config="hw_config",
        build_root=f"build/debug/atan2_order_{order}"
    )

    sys.path.insert(0, str(ws / "tests" / "operators"))
    from utils.data_generator import ArithTestDataGenerator

    fy = 0.2
    fx = 1.1
    input_data = {"y_in": fy, "x_in": fx}
    
    # Golden for manual step-by-step
    dx_v = fx - 1.0
    recip_v = 1.0
    for _ in range(order):
        recip_v = 1.0 - dx_v * recip_v
    z_v = fy * recip_v
    z2_v = z_v * z_v
    if order == 1: atan_v = z_v
    elif order == 3: atan_v = z_v * (1.0 - z2_v * 0.3333333333)
    elif order >= 5: atan_v = z_v * (1.0 - z2_v * 0.3333333333 + z2_v * z2_v * 0.2)
    else: atan_v = z_v
    
    start_time = time.time()
    try:
        success = runner.run_full_pipeline(
            op_name="atan2",
            get_dfg_fn=lambda: get_atan2_dfg(order=order),
            input_data=input_data,
            golden_y=atan_v
        )
        duration = time.time() - start_time
        if success:
            print(f"✅ Order {order} PASSED ({duration:.1f}s)")
            return True
        else:
            print(f"❌ Order {order} FAILED Simulation ({duration:.1f}s)")
            return False
    except Exception as e:
        duration = time.time() - start_time
        print(f"🔥 Order {order} CRASHED/UNSAT ({duration:.1f}s): {e}")
        return False


if __name__ == "__main__":
    orders = [1, 3, 5, 7]
    results = {}
    for o in orders:
        results[o] = run_order_test(o)
        if not results[o]:
            print(f"\nEarly exit at Order {o}. Resource limit reached.")
            break
    
    print("\nSummary:")
    for o, res in results.items():
        print(f"  Order {o}: {'✅' if res else '❌'}")
