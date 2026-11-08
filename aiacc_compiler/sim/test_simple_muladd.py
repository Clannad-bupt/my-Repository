import sys, os, json
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'src'))

from synthesis.ucode_solver import UCodeSynthesizer
from api.hw_spec import HardwareSpec
from sim.tests.helpers import make_op
from driver.assembler import PhysicalAssembler, PhysicalInstruction
from driver.emitter import LIRPacker
from typing import List, Dict, Any
import numpy as np

def run_simple_muladd_test():
    # 1. 环境准备
    hw_config_dir = os.path.join(os.path.dirname(__file__), '..', 'hw_config')
    hw_spec = HardwareSpec.load(hw_config_dir)
    synthesizer = UCodeSynthesizer(hw_spec)
    
    # 构造 DFG: y = c3 * x + c2
    # x = [0.1, 0.2, 0.3, 0.4]
    # c3 = -0.1666, c2 = 1.0
    # Expected y = -0.01666 + 1.0 = 0.98333
    ops = [
        make_op("UR_READ", "rd_x", logical_addr=0, stride=1),
        make_op("RTOVR",   "rt_x", deps=["rd_x"]),
        make_op("UR_READ", "rd_c3", logical_addr=256, stride=0),
        make_op("RTOVR",   "rt_c3", deps=["rd_c3"]),
        make_op("FPMUL",   "mul1",  deps=["rt_c3", "rt_x"]), 
        make_op("RTOVR",   "rt_m1", deps=["mul1"]),
        
        make_op("UR_READ", "rd_c2", logical_addr=257, stride=0),
        make_op("RTOVR",   "rt_c2", deps=["rd_c2"]),
        make_op("FPADD",   "add1",  deps=["rt_m1", "rt_c2"]), 
        make_op("RTOVR",   "rt_a1", deps=["add1"]),
        
        make_op("UR_WRITE", "wr_y", deps=["rt_a1"], logical_addr=512, stride=1)
    ]
    
    print("\n[STEP 1] Solving SMT Schedule...")
    result = synthesizer.solve(ops, II=3)
    if not result:
        print("❌ SMT Solver Failed for II=3")
        return
        
    schedule, unit_assignments = result
    print(f"✅ SMT Solved: II=3")
    for op_id, t in schedule.items():
        print(f"  {op_id:<10}: t={t}, unit={unit_assignments[op_id]}")

    # 2. 注入数据
    from sim.physical_runner import PhysicalProgram
    program = PhysicalProgram()
    # x = 0.1, 0.2, 0.3, 0.4
    data_x = np.array([0.1, 0.2, 0.3, 0.4], dtype=np.float32)
    program.inject_vector(0, data_x)
    program.inject_broadcast(256, -0.166666)
    program.inject_broadcast(257, 1.0)

    # 3. 装配指令
    assembler = PhysicalAssembler(hw_spec)
    # 模拟 4 行数据 (1 次迭代)
    instructions = assembler.assemble(ops, schedule, unit_assignments, II=3, num_iterations=1)
    
    # 4. 生成预期值 (Golden)
    # y = c3 * x + c2 = -0.166666 * 0.1 + 1.0 = 0.9833334
    expected_val = -0.166666 * 0.1 + 1.0
    print(f"\n[STEP 2] Expected Value: {expected_val}")

    # 5. 运行仿真
    from sim.physical_runner import PhysicalRunner
    runner = PhysicalRunner(hw_spec)
    print("\n[STEP 3] Running Simulation...")
    # 记录日志到 simple_test.log
    with open("simple_test.log", "w") as f:
        sys.stdout = f
        runner.run(instructions, program, max_cycles=60)
        sys.stdout = sys.__stdout__

    # 6. 数值审计
    print("\n[STEP 4] Numerical Audit (Cycle 19-21)")
    # 我们查找 add1 的执行
    t_add = schedule["add1"]
    cyc_add = 2 + t_add
    print(f"Searching for add1 at Cycle {cyc_add}...")
    
    with open("simple_test.log", "r") as f:
        logs = f.readlines()
        for line in logs:
            if f"Cyc:{cyc_add}" in line and "FPADD" in line and "COMPUTE" in line:
                print(f"Found: {line.strip()}")

if __name__ == "__main__":
    run_simple_muladd_test()
