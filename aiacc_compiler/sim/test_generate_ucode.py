import os
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'src'))

from synthesis.ucode_solver import UCodeSynthesizer
from synthesis.encoder import SMIEncoder
from api.hw_spec import HardwareSpec
from tests.helpers import make_op

def test_horner_3rd_ucode():
    spec_dir = os.path.join(os.path.dirname(__file__), '..', 'hw_config')
    spec = HardwareSpec.load(spec_dir)
    
    # 1. 构造三阶 Horner 链: y = a + x * (b + x * (c + x * d))
    # 资源：3 个 FPMUL, 3 个 FPADD, 5 个 UR_READ
    ops = [
        make_op("UR_READ", "x"),
        make_op("UR_READ", "a"),
        make_op("UR_READ", "b"),
        make_op("UR_READ", "c"),
        make_op("UR_READ", "d"),
        
        make_op("FPMUL", "mul1", deps=["x", "d"]),
        make_op("FPADD", "add1", deps=["mul1", "c"]),
        make_op("FPMUL", "mul2", deps=["x", "add1"]),
        make_op("FPADD", "add2", deps=["mul2", "b"]),
        make_op("FPMUL", "mul3", deps=["x", "add2"]),
        make_op("FPADD", "add3", deps=["mul3", "a"]),
        
        make_op("UR_WRITE", "out", deps=["add3"])
    ]
    
    # 将 x 设为不变量，减少 Bank/端口压力
    for op in ops:
        if op.trace_id == "x":
            op.metadata["is_invariant"] = True

    # 2. 调度 (强制 II=3，模拟单乘法器瓶颈)
    synth = UCodeSynthesizer(spec)
    result = synth.solve_with_units(ops, MII=3, MAX_II=5)
    
    if not result:
        print("调度失败")
        return

    print(f"\n>>> 三阶 Horner 链调度成功，II = {result['II']}")
    
    # 3. 按硬件单元（Unit）生成 Hex
    unit_map = result["unit_assignments"] 
    encoder = SMIEncoder(ii=result["II"])
    
    # 获取所有参与工作的硬件单元列表
    active_units = sorted(list(set(unit_map.values())))
    
    print("\n" + "="*35 + " 每个硬件单元的物理 Hex 指令流 (II=3) " + "="*35)
    for unit in active_units:
        print(f"\n[Hardware Unit: {unit}]")
        print("-" * 50)
        hex_stream = encoder.generate_unit_hex(unit, ops, result["schedule"], unit_map, loop_count=100)
        for s, hex_line in enumerate(hex_stream):
            print(f"Slot {s}: {hex_line}")
    print("\n" + "="*110)

if __name__ == "__main__":
    test_horner_3rd_ucode()
