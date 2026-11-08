import os
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'src'))

from synthesis.ucode_solver import UCodeSynthesizer
from synthesis.encoder import SMIEncoderTruth
from api.hw_spec import HardwareSpec
from tests.helpers import make_op, MockSpec
from sim.test_complex_synthesis import print_pipeline_timeline

def debug_mac_alignment_ii1():
    # 1. 使用 MockSpec 确保资源充足，支持 II=1
    # 核心：必须有 2 个及以上的 RTOVR 实例来支撑同一拍的 rt_a, rt_b
    spec = MockSpec(
        instances={"FPMUL": 1, "FPADD": 1, "UR_READ": 4, "UR_WRITE": 4, "RTOVR": 8},
        delays={"FPMUL": 4, "FPADD": 4, "UR_READ": 1, "UR_WRITE": 1, "RTOVR": 1}
    )
    
    # 2. 构造完整的物理 DFG (a*b+c)
    ops = [
        make_op("UR_READ", "rd_a"),      # T=0
        make_op("UR_READ", "rd_b"),      # T=0
        make_op("RTOVR",   "rt_a",  deps=["rd_a"]), 
        make_op("RTOVR",   "rt_b",  deps=["rd_b"]), 
        make_op("FPMUL",   "mul",   deps=["rt_a", "rt_b"]), 
        
        make_op("UR_READ", "rd_c"),      
        make_op("RTOVR",   "rt_c",  deps=["rd_c"]), 
        make_op("RTOVR",   "rt_mul",deps=["mul"]),  
        make_op("FPADD",   "add",   deps=["rt_c", "rt_mul"]), 
        
        make_op("RTOVR",   "rt_add",deps=["add"]),  
        make_op("UR_WRITE", "wr_out", deps=["rt_add"]) 
    ]
    
    # 3. 调度 (让 SMT 自动搜索最小 II)
    synth = UCodeSynthesizer(spec)
    result = synth.solve_with_units(ops, MII=1, MAX_II=10)
    
    if not result:
        print("即便使用 Mock 资源，II=6 依然 UNSAT。请检查 RAW 依赖链。")
        return

    schedule = result["schedule"]
    unit_map = result["unit_assignments"]
    II = result["II"]

    # 4. 输出物理时序图
    print_pipeline_timeline(ops, schedule, II)
    
    # 5. 输出 TRUTH-ALIGNED MICRO-INSTRUCTION DUMP
    from synthesis.encoder import SMIEncoderTruth
    encoder = SMIEncoderTruth(ii=II)
    
    # 假设迭代 10 次，步长为 1 -> INC = 1 * II
    dump_str = encoder.dump_truth_aligned(ops, schedule, unit_map, loop_count=10, base_stride=1)
    print("\n" + dump_str)
    print("\n" + "="*100)

if __name__ == "__main__":
    debug_mac_alignment_ii1()
