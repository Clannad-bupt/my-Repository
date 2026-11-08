import sys, os, json
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'src'))

from synthesis.ucode_solver import UCodeSynthesizer
from api.hw_spec import HardwareSpec
from sim.tests.helpers import make_op
from sim.test_complex_synthesis import print_pipeline_timeline

# 🛸 [NEW] 导入物理驱动层组件
from driver.assembler import PhysicalAssembler, PhysicalInstruction
from driver.emitter import LIRPacker
from typing import List, Dict, Any
import numpy as np

# 🛸 [NEW] 导入数据生成器
ws_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
sys.path.insert(0, os.path.join(ws_root, "tests", "operators"))
from utils.data_generator import ArithTestDataGenerator

# 🛸 [V27.5] 调试日志开关
DEBUG_VERBOSE = True
if not DEBUG_VERBOSE:
    import logging
    logging.getLogger("ParallelKernel").setLevel(logging.WARNING)

class MockProgram:
    class MockAllocator:
        def __init__(self):
            # 初始化 8 个 Bank，每个 Bank 256 行，每行 4 个 Lane (SIMD)
            self.banks = {i: [np.zeros(4, dtype=np.float32) for _ in range(256)] for i in range(8)}
    def __init__(self):
        self.memory_allocator = self.MockAllocator()
        self.tensors = {}
        
    def inject_vector(self, logical_addr: int, data: np.ndarray):
        """注入矢量数据 (如 x)"""
        bank_id = logical_addr // 256
        offset = logical_addr % 256
        # 填充行数据
        num_rows = (len(data) + 3) // 4
        for r in range(num_rows):
            row_data = data[r*4 : (r+1)*4]
            # 如果不足 4 个，补齐
            if len(row_data) < 4:
                row_data = np.pad(row_data, (0, 4 - len(row_data)))
            self.memory_allocator.banks[bank_id][offset + r] = row_data

    def inject_broadcast(self, logical_addr: int, scalar: float):
        """将标量广播到整个 SIMD 行 (如系数 c)"""
        bank_id = logical_addr // 256
        offset = logical_addr % 256
        self.memory_allocator.banks[bank_id][offset] = np.full(4, scalar, dtype=np.float32)

def print_physical_instructions(instructions: List[PhysicalInstruction]):
    print("\n" + "="*40 + " SIMD-ALIGNED HORNER TAYOR DUMP (4 ROWS) " + "="*40)
    current_iq = None
    for instr in instructions:
        if instr.iq_name != current_iq:
            current_iq = instr.iq_name
            print(f"\n--- Unit: {current_iq} ---")
        h = instr.header
        # 🛸 [FIX V27] 修正打印逻辑：EMB 对应物理位域 EMBED_LOOPS (h.level)
        h_str = f"VLD:{h.vld}|EMB:{h.level}|E_END:{h.embed_end}|LOOP:{h.loops}|INC:{h.inc}|INC_E:{h.inc_embed}"
        if instr.payload:
            p_items = []
            keys = sorted(instr.payload.keys())
            for k in ["SMC_ID", "VALID"]:
                if k in keys: keys.remove(k); keys.insert(0, k)
            for k in keys:
                v = instr.payload[k]
                if k.endswith("_PREC"):
                    label = "FP32" if v == 1 else "FP16"
                    p_items.append(f"{k}:{label}")
                elif v != 0 or k in ["VALID", "SMC_ID", "SEL", "ADDR", "BANK"]:
                    p_items.append(f"{k}:{v}")
            b_str = "|".join(p_items)
        else: b_str = "NOP"
        stage_label = f"[{instr.stage}]" if instr.stage != "BODY" else f"[{instr.local_id}]"
        print(f"{instr.iq_name:<14} {stage_label:<7} : HEAD:[{h_str}] | BODY:[{b_str}] /* Op: {instr.trace_id} */ ")

def print_aligned_timeline(expected_schedule, actual_activity, II, max_cycles=60):
    """
    [V27.5] 垂直时序对齐图：同时展示 SMT 预期与模拟器实际执行。
    """
    print(f"\n========================= 物理时序图 (II={II}) =========================")
    print(f"{'Cycle':<5} | {'Iter 0':<25} | {'Iter 1':<25} | {'Iter 2':<25} |")
    print("-" * 90)
    
    # 辅助：获取符号
    def get_sym(unit_name):
        u = unit_name.upper()
        if "MUL" in u: return 'm'
        if "ADD" in u: return 'a'
        if "RTOVR" in u: return 'r'
        if "UR-R" in u: return 'L'
        if "UR-W" in u: return 'S'
        return 'X'

    # 反查表：Actual Char -> Op Name (简略版)
    sym_to_type = {'m': 'mul', 'a': 'add', 'r': 'rt', 'L': 'rd', 'S': 'wr'}

    for c in range(max_cycles):
        row = f"{c:<5} | "
        for iter_id in range(3):
            # 1. 理论预期 (基于 SMT 调度)
            expected_ops = []
            for op_id, t_base in expected_schedule.items():
                if 2 + t_base + iter_id * II == c:
                    expected_ops.append(op_id)
            
            # 2. 实际执行 (基于仿真轨迹)
            actual_ops = []
            for iq_name, trace in actual_activity.items():
                if c < len(trace):
                    char = trace[c]
                    if char in sym_to_type:
                        # 简略匹配：如果 iq_name 包含该类型，则认为是在执行
                        # 这只是一个示意，真实匹配需要解析指令 Payload
                        actual_ops.append(f"{char}")
            
            exp_str = ",".join(expected_ops)
            act_str = "".join(actual_ops)
            
            if exp_str and act_str:
                cell = f"{exp_str:<15} [ACT:{act_str}]"
            elif exp_str:
                cell = f"{exp_str:<15} [MISS]"
            elif act_str:
                cell = f"{' ':<15} [STRAY:{act_str}]"
            else:
                cell = ""
            
            row += f"{cell:<25} | "
        print(row)
    print("=" * 90)

def test_horner_production_alignment():
    print("\n" + "="*20 + " SIMD 3rd-Order Horner Taylor: 4-Row Test " + "="*20)
    spec = HardwareSpec.load("hw_config")
    
    # 🛸 [NEW] 准备 16 个元素    # 🛸 [V27.5] 生成测试数据 (SIMD 4-lane x 4 iterations = 16 rows)
    n_rows = 16
    data_x = np.linspace(0.1, 1.6, n_rows).astype(np.float32)
    coeffs = [-0.166666, 0.0, 1.0, 0.0]
    
    # 2. 构造 DFG (Full 3rd-Order Horner: ((c3*x + c2)*x + c1)*x + c0)
    ops = [
        # Stage 1: ans1 = c3 * x + c2
        make_op("UR_READ", "rd_x1", logical_addr=0, stride=1),
        make_op("RTOVR",   "rt_x1", deps=["rd_x1"]),
        make_op("UR_READ", "rd_c3", logical_addr=256, stride=0),
        make_op("RTOVR",   "rt_c3", deps=["rd_c3"]),
        make_op("FPMUL",   "mul1",  deps=["rt_c3", "rt_x1"]), 
        make_op("RTOVR",   "rt_m1", deps=["mul1"]),
        
        make_op("UR_READ", "rd_c2", logical_addr=257, stride=0),
        make_op("RTOVR",   "rt_c2", deps=["rd_c2"]),
        make_op("FPADD",   "add1",  deps=["rt_m1", "rt_c2"]), 
        make_op("RTOVR",   "rt_a1", deps=["add1"]),
        
        # Stage 2: ans2 = ans1 * x + c1
        make_op("UR_READ", "rd_x2", logical_addr=0, stride=1),
        make_op("RTOVR",   "rt_x2", deps=["rd_x2"]),
        make_op("UR_READ", "rd_c1", logical_addr=258, stride=0),
        make_op("RTOVR",   "rt_c1", deps=["rd_c1"]),
        make_op("FPMUL",   "mul2",  deps=["rt_a1", "rt_x2"]), 
        make_op("RTOVR",   "rt_m2", deps=["mul2"]),
        
        make_op("FPADD",   "add2",  deps=["rt_m2", "rt_c1"]), 
        make_op("RTOVR",   "rt_a2", deps=["add2"]),

        # Stage 3: ans3 = ans2 * x + c0
        make_op("UR_READ", "rd_x3", logical_addr=0, stride=1),
        make_op("RTOVR",   "rt_x3", deps=["rd_x3"]),
        make_op("UR_READ", "rd_c0", logical_addr=259, stride=0),
        make_op("RTOVR",   "rt_c0", deps=["rd_c0"]),
        make_op("FPMUL",   "mul3",  deps=["rt_a2", "rt_x3"]), 
        make_op("RTOVR",   "rt_m3", deps=["mul3"]),
        
        make_op("FPADD",   "add3",  deps=["rt_m3", "rt_c0"]), 
        make_op("RTOVR",   "rt_a3", deps=["add3"]),
        
        make_op("UR_WRITE", "wr_y", deps=["rt_a3"], logical_addr=512, stride=1)
    ]
    
    # 3. 调度与装配
    synth = UCodeSynthesizer(spec)
    result = synth.solve_with_units(ops, MII=1, MAX_II=32)
    if not result: return
    schedule, II, unit_map = result["schedule"], result["II"], result.get("unit_assignments", {})
    for op in ops:
        op.metadata["global_t"] = schedule[op.trace_id]
        op.metadata["unit_id"] = unit_map.get(op.trace_id)

    assembler = PhysicalAssembler(spec)
    # 🛸 [NEW] 设定循环次数为 4 (16 elements / 4 lanes)，硬件 Header 将自动生成 LOOP:3
    num_loop_iters = n_rows // 4
    physical_instrs = assembler.assemble(ops, II=II, num_iterations=num_loop_iters)

    # 4. 数据注入与 Golden 计算
    program = MockProgram()
    program.inject_vector(0, data_x)        # x 填入 Bank 0, 占 4 行
    program.inject_broadcast(256, coeffs[0]) 
    program.inject_broadcast(257, coeffs[1]) 
    program.inject_broadcast(258, coeffs[2]) 
    program.inject_broadcast(259, coeffs[3]) 
    
    # 🛸 [NEW] 计算 Golden 结果 (Horner 3rd: ((c3*x + c2)*x + c1)*x + c0)
    c3, c2, c1, c0 = coeffs
    
    # 🛸 [V27.5] 计算全链路中间变量与输入用于 Rigid Audit (SIMD 采样 Lane 0)
    golden_intermediates = []
    num_iterations = 4
    for iter_idx in range(num_iterations):
        x_val = data_x[iter_idx * 4]  # Lane 0
        m1 = x_val * c3
        a1 = m1 + c2
        m2 = a1 * x_val
        a2 = m2 + c1
        m3 = a2 * x_val
        a3 = m3 + c0
        
        golden_intermediates.append({
            "iter": iter_idx,
            "x": float(x_val),
            "mul1_in": [float(x_val), float(c3)], "mul1_out": float(m1),
            "add1_in": [float(m1), float(c2)], "add1_out": float(a1),
            "mul2_in": [float(a1), float(x_val)], "mul2_out": float(m2),
            "add2_in": [float(m2), float(c1)], "add2_out": float(a2),
            "mul3_in": [float(a2), float(x_val)], "mul3_out": float(m3),
            "add3_in": [float(m3), float(c0)], "add3_out": float(a3),
            "rd_x_val": float(x_val),
            "rd_c3_val": float(c3),
            "rd_c2_val": float(c2),
            "rd_c1_val": float(c1),
            "rd_c0_val": float(c0)
        })
    
    # 🛸 [NEW] Golden Y 计算
    golden_y = ((c3 * data_x + c2) * data_x + c1) * data_x + c0
    
    # 🛸 [NEW] 注册 Tensor 元数据用于仿真器对齐
    from collections import namedtuple
    TensorObj = namedtuple("TensorObj", ["bank", "offset", "shape", "role"])
    program.tensors["x"] = TensorObj(bank=0, offset=0, shape=(num_loop_iters, 4), role="input")
    program.tensors["y"] = TensorObj(bank=2, offset=0, shape=(num_loop_iters, 4), role="output")

    # 5. 导出并打印流水线纵向时序图
    output_dir = "build/alignment/horner_3rd"
    os.makedirs(output_dir, exist_ok=True)
    
    # 保存 Golden 数据与预期中间值
    np.savez(os.path.join(output_dir, "golden_data.npz"), y=golden_y)
    with open(os.path.join(output_dir, "expected_values.json"), "w") as f:
        json.dump(golden_intermediates, f, indent=2)
    
    print("\n" + "="*25 + f" 物理流水线时序图 (II={II}) " + "="*25)
    t_map = {op.trace_id: schedule[op.trace_id] for op in ops}
    print_pipeline_timeline(t_map, schedule, II, max_cycles=60)
    
    print_physical_instructions(physical_instrs)
    packer = LIRPacker("hw_config")
    packer.emit_from_instructions(physical_instrs, output_dir, program=program)
    # 6. 导出调度对齐参考
    schedule_dump = {
        "II": II,
        "ops": {op.trace_id: {"t": int(op.metadata["global_t"]), "iq": op.metadata["unit_id"]} for op in ops}
    }
    with open(os.path.join(output_dir, "compiler_schedule.json"), "w") as f:
        json.dump(schedule_dump, f, indent=2)

    # 🛸 [NEW] 同步更新 timeline_ascii.txt 到 smc_0/analysis/
    analysis_dir = os.path.join(output_dir, "smc_0", "analysis")
    os.makedirs(analysis_dir, exist_ok=True)
    timeline_path = os.path.join(analysis_dir, "timeline_ascii.txt")
    
    with open(timeline_path, "w") as f:
        f.write(f"========================= 物理流水线时序图 (II={II}) =========================\n")
        # 捕获 print_pipeline_timeline 的输出并写入文件
        from io import StringIO
        import contextlib
        with StringIO() as buf, contextlib.redirect_stdout(buf):
            print_pipeline_timeline(t_map, schedule, II, max_cycles=60)
            f.write(buf.getvalue())
    
    print(f"\n[SYNC] 物理时序图已同步至: {timeline_path}")

    # 🚀 [V27.0 INTEGRATION] 启动全流程自动化校验
    print("\n" + "="*20 + " 🛡️  AIACC FULL PIPELINE VERIFICATION " + "="*20)
    import subprocess
    
    # 1. 运行物理仿真 (生成真实的 timeline_ascii.txt)
    print("\n[STEP 1] Running Physical Simulation...")
    # 🛸 [V27.5] 重定向输出到 test_output.log 以供审计
    with open("test_output.log", "w") as log_f:
        sim_cmd = ["python3", "sim/smc_simulator/tools/physical_runner.py", output_dir]
        res = subprocess.run(sim_cmd, stdout=log_f, stderr=log_f)
        if res.returncode != 0:
            print(f"❌ [ERROR] Physical simulation failed. Check test_output.log for details.")
            sys.exit(1)
    
    # 🛸 [V27.5] 解析实际轨迹用于对齐图展现
    actual_activity = {}
    if os.path.exists(timeline_path):
        with open(timeline_path, "r") as f:
            for line in f:
                if "|" in line and "iq_name" not in line and "occupancy" not in line:
                    parts = line.split("|")
                    if len(parts) >= 2:
                        name = parts[0].strip()
                        trace = parts[1].strip().split()
                        actual_activity[name] = trace

    # 🛸 [V27.5] 打印对齐后的垂直时序图
    print_aligned_timeline(schedule, actual_activity, II, max_cycles=50)

    # 2. 运行物理审计 (检查 HDR/EMB/INC_EMBED 字段)
    print("\n[STEP 2] Running Physical Header Audit...")
    audit_cmd = ["python3", "sim/audit_physical_headers.py", output_dir]
    audit_res = subprocess.run(audit_cmd)
    
    # 3. 运行真值时序对齐校验 (包含数值对齐审计)
    print("\n[STEP 3] Running Truth Alignment Verification (Timing & Numerical)...")
    verify_cmd = ["python3", "sim/verify_truth_alignment.py"]
    verify_res = subprocess.run(verify_cmd)
    
    if audit_res.returncode == 0 and verify_res.returncode == 0:
        print("\n" + "✨"*10 + " ALL PHYSICAL CHECKS PASSED " + "✨"*10)
    else:
        print("\n" + "❌"*10 + " PIPELINE VERIFICATION FAILED " + "❌"*10)
        sys.exit(1)

    print(f"\n[DONE] 4-Row SIMD Test & Verification Completed.")

if __name__ == "__main__":
    test_horner_production_alignment()
