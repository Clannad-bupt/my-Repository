import sys
from pathlib import Path
import sympy as sp
import copy

# 🛸 设置路径
ws = Path.cwd()
sys.path.append(str(ws / "src"))

from frontend import GraphCompiler
from api.hw_spec import HardwareSpec
from driver.assembler import PhysicalAssembler
from driver.emitter import LIRPacker
from ir.lir.lir_op import LIROp

def make_manual_dfg():
    """构造手动优化的 3 阶 Horner DFG"""
    ops = []
    def add_op(op_type, trace_id, deps=[]):
        op = LIROp(op_type, trace_id)
        op.metadata['depends_on'] = deps
        ops.append(op)
        return trace_id

    # Stage 1: c3*x1 + c2
    add_op("UR_READ", "rd_x1")
    add_op("RTOVR",   "rt_x1", deps=["rd_x1"])
    add_op("FPMUL",   "mul1",  deps=["rt_x1"]) 
    add_op("RTOVR",   "rt_m1", deps=["mul1"])
    add_op("FPADD",   "add1",  deps=["rt_m1"]) 
    add_op("RTOVR",   "rt_a1", deps=["add1"])
    
    # Stage 2: (ans1)*x2 + c1
    add_op("UR_READ", "rd_x2")
    add_op("RTOVR",   "rt_x2", deps=["rd_x2"])
    add_op("FPMUL",   "mul2",  deps=["rt_a1", "rt_x2"]) 
    add_op("RTOVR",   "rt_m2", deps=["mul2"])
    add_op("FPADD",   "add2",  deps=["rt_m2"]) 
    add_op("RTOVR",   "rt_a2", deps=["add2"])
    
    # Stage 3: (ans2)*x3 + c0
    add_op("UR_READ", "rd_x3")
    add_op("RTOVR",   "rt_x3", deps=["rd_x3"])
    add_op("FPMUL",   "mul3",  deps=["rt_a2", "rt_x3"]) 
    add_op("RTOVR",   "rt_m3", deps=["mul3"])
    add_op("FPADD",   "add3",  deps=["rt_m3"]) 
    add_op("RTOVR",   "rt_a3", deps=["add3"])
    
    add_op("UR_WRITE", "wr_y", deps=["rt_a3"])
    return ops

def run_comparison():
    spec = HardwareSpec.load("hw_config")
    assembler = PhysicalAssembler(spec)
    packer = LIRPacker("hw_config")

    # --- 1. 手动版 ---
    print("\n" + "="*30 + " [MODE 1] MANUAL OPTIMIZED DFG " + "="*30)
    manual_ops = make_manual_dfg()
    from synthesis.ucode_solver import UCodeSynthesizer
    synth = UCodeSynthesizer(spec)
    
    m_res = synth.solve_with_units(manual_ops, MII=3, MAX_II=10)
    if m_res:
        for op in manual_ops: op.metadata["global_t"] = m_res["schedule"][op.trace_id]
        m_instrs = assembler.assemble(manual_ops, II=m_res["II"], num_iterations=10)
        packer.dump_verbose(m_instrs)
    
    # --- 2. 编译器版 ---
    print("\n" + "="*30 + " [MODE 2] COMPILER AUTO-GENERATED DFG " + "="*30)
    compiler = GraphCompiler()
    
    # 定义符号
    x1, x2, x3 = sp.symbols('x1 x2 x3')
    c0, c1, c2, c3 = sp.symbols('c0 c1 c2 c3')
    
    # Horner 公式: ((c3*x1 + c2)*x2 + c1)*x3 + c0
    formula = ((c3 * x1 + c2) * x2 + c1) * x3 + c0
    
    # 定义输入角色 (编译器需要知道哪些是外部输入)
    inputs_meta = {
        x1: {"role": "operand"}, x2: {"role": "operand"}, x3: {"role": "operand"},
        c0: {"role": "operand"}, c1: {"role": "operand"}, c2: {"role": "operand"}, c3: {"role": "operand"}
    }
    
    # 自动生成 DFG
    auto_ops = compiler.compile(inputs_meta, formula, out_name="y_out")
    
    # 对接物理后端
    a_res = synth.solve_with_units(auto_ops, MII=3, MAX_II=10)
    if a_res:
        for op in auto_ops: op.metadata["global_t"] = a_res["schedule"][op.trace_id]
        a_instrs = assembler.assemble(auto_ops, II=a_res["II"], num_iterations=10)
        packer.dump_verbose(a_instrs)
    else:
        print("❌ Compiler DFG UNSAT! (可能是依赖解析错误或资源冲突)")

if __name__ == "__main__":
    run_comparison()
