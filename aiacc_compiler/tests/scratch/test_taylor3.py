import numpy as np
import sys
import os

# 🛰️ [WORKSPACE ALIGNMENT] 
# Ensure we can import from src/ and sim/
sys.path.append(os.path.join(os.getcwd(), "src"))

from api.op_tester import OperatorPhysicalTester
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

def get_taylor3_dfg():
    b = MIROpBuilder()
    
    # --- [CONSTANTS] ---
    c1 = b.make_op("UR_READ", "c1_inv6", RT.UR_READ, [], is_const=True, value=0.1666666667, role="constant")
    c2 = b.make_op("UR_READ", "c2_one",  RT.UR_READ, [], is_const=True, value=1.0,          role="constant")

    # --- [INPUTS] ---
    x_in = b.make_op("UR_READ", "x_in", RT.UR_READ, [], role="operand_a")

    # --- [STAGE 1: x_sq = x * x] ---
    r0_y = b.make_op("RTOVR", "r0_y", PS.FPMUL_S0, [x_in])
    r1_y = b.make_op("RTOVR", "r1_y", PS.FPMUL_S1, [x_in])
    x_sq = b.make_op("FPMUL", "x_sq", RT.FPMUL, [r0_y, r1_y])
    
    # Logical persistence point
    w_sq = b.make_op("UR_WRITE", "w_sq", RT.UR_WRITE, [b.make_op("RTOVR", "v_sq", PS.UR_W0_S, [x_sq])])

    # --- [STAGE 2: p1 = 0.1666 * x_sq] ---
    x_sq_re = b.make_op("UR_READ", "x_sq_re", RT.UR_READ, [w_sq])
    p1 = b.make_op("FPMUL", "p1", RT.FPMUL, [
        b.make_op("RTOVR", "p1_m0", PS.FPMUL_S0, [c1]),
        b.make_op("RTOVR", "p1_m1", PS.FPMUL_S1, [x_sq_re])
    ])

    # --- [STAGE 3: p2 = 1.0 - p1] ---
    p2 = b.make_op("FPADD", "p2", RT.FPADD, [
        b.make_op("RTOVR", "p2_a0", PS.FPADD_S0, [c2]),
        b.make_op("RTOVR", "p2_a1", PS.FPADD_S1, [p1])
    ], SUB=1)

    # --- [STAGE 4: res = x * p2] ---
    res = b.make_op("FPMUL", "taylor_res", RT.FPMUL, [
        b.make_op("RTOVR", "fin_m0", PS.FPMUL_S0, [x_in]),
        b.make_op("RTOVR", "fin_m1", PS.FPMUL_S1, [p2])
    ])

    # --- [OUTPUT] ---
    b.make_op("UR_WRITE", "y_out", RT.UR_WRITE, [
        b.make_op("RTOVR", "fout", PS.UR_W1_S, [res])
    ], role="output", meta_name="y")
    
    return b.get_dfg()

def test_taylor3():
    tester = OperatorPhysicalTester(hw_config_path="hw_config", build_root="build/scratch")
    
    # Prepare directories manually to save golden data
    op_name = "taylor3_op"
    strategy = "balanced"
    op_root = tester.build_root / op_name.lower()
    acb_path = op_root / f"perf_contest/{strategy}/phase2_emission/acb_bundle"
    acb_path.mkdir(parents=True, exist_ok=True)

    # Mock data
    x_val = 0.5
    input_data = {"x_in": np.array([x_val] * 4)}
    golden_y = x_val * (1.0 - 0.1666666667 * (x_val**2))
    golden_y_vec = np.array([golden_y] * 4)
    
    # Save Golden Data for the simulator
    np.savez(acb_path / "golden_data.npz", y=golden_y_vec)
    print(f"Saved Golden Data to {acb_path / 'golden_data.npz'}")
    
    dfg = get_taylor3_dfg()
    
    success, msg = tester.run_full_stack_verification(
        op_name="taylor3_op",
        dfg_ops=dfg,
        input_data=input_data,
        golden_y=golden_y_vec,
        strategies=["balanced"]
    )
    
    if success:
        print("\n✅ Taylor3 Operator Verification PASSED!")
    else:
        print(f"\n❌ Taylor3 Operator Verification FAILED: {msg}")

if __name__ == "__main__":
    test_taylor3()
