import os
import sys

sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'src'))

from api.hw_spec import HardwareSpec
from ir.mir.mir_op import MIROp
from synthesis.backend import SMTSynthesizer
from core.ii_analyzer import IIAnalyzer

def print_pipeline_timeline(ops, schedule, II, max_cycles=15):
    """
    可视化模调度流水线时序图 (Bit-Perfect Pipeline View)
    """
    print(f"\n{'='*25} 物理时序图 (II={II}) {'='*25}")
    
    # 查找最大周期
    total_cycles = max(schedule.values()) + 1
    display_cycles = min(total_cycles + II * 3, max_cycles)
    
    # 构建表头 (Iteration 0, 1, 2...)
    header = "Cycle | "
    for iter_id in range(4):
        header += f"Iter {iter_id:<12} | "
    print(header)
    print("-" * len(header))

    for c in range(display_cycles):
        row = f"{c:<5} | "
        for iter_id in range(4):
            # T = base_t + k*II
            current_ops = []
            for op_id, t_base in schedule.items():
                if t_base + iter_id * II == c:
                    current_ops.append(op_id)
            
            cell = ",".join(current_ops)
            row += f"{cell:<17} | "
        print(row)
    print("=" * 70)
    print("注：同一 Cycle 下不同 Iter 的算子竞争的是物理 Slot $s = T \pmod{II}$")

def main():
    hw_config_dir = os.path.join(os.path.dirname(__file__), '..', 'hw_config')
    spec = HardwareSpec.load(hw_config_dir)

    # 构造 a * b + c 的 DFG
    ops = [
        MIROp(op_type="UR_READ",  logic_cycle=0, trace_id="read_a",  metadata={"manual_bank": 0}),
        MIROp(op_type="UR_READ",  logic_cycle=0, trace_id="read_b",  metadata={"manual_bank": 1}),
        MIROp(op_type="UR_READ",  logic_cycle=0, trace_id="read_c",  metadata={"manual_bank": 2}),
        MIROp(op_type="RTOVR",    logic_cycle=1, trace_id="rt_a",    metadata={"depends_on": ["read_a"]}),
        MIROp(op_type="RTOVR",    logic_cycle=1, trace_id="rt_b",    metadata={"depends_on": ["read_b"]}),
        MIROp(op_type="RTOVR",    logic_cycle=1, trace_id="rt_c",    metadata={"depends_on": ["read_c"]}),
        MIROp(op_type="FPMUL",    logic_cycle=2, trace_id="mul_ab",  metadata={"depends_on": ["rt_a", "rt_b"]}),
        MIROp(op_type="RTOVR",    logic_cycle=3, trace_id="rt_mul",  metadata={"depends_on": ["mul_ab"]}),
        MIROp(op_type="FPADD",    logic_cycle=4, trace_id="add_c",   metadata={"depends_on": ["rt_mul", "rt_c"]}),
        MIROp(op_type="RTOVR",    logic_cycle=5, trace_id="rt_add",  metadata={"depends_on": ["add_c"]}),
        MIROp(op_type="UR_WRITE", logic_cycle=6, trace_id="write_out", metadata={"depends_on": ["rt_add"], "manual_bank": 3}),
    ]

    synth = SMTSynthesizer(spec)
    mii = IIAnalyzer(spec).compute(ops)

    print("\n" + "=" * 60)
    print(f"【模调度】MII={mii}，MAX_II=20")
    print("=" * 60)

    result = synth.solve(ops, MII=mii, MAX_II=20)

    if result:
        print_pipeline_timeline(ops, result["schedule"], result["II"])
    else:
        print("调度失败")

if __name__ == "__main__":
    main()
