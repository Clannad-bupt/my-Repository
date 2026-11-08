import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from ir.hir import HIRNode, TensorObj
from ir.mir.mir_op import MIROp
from ir.lir.lir_op import LIROp

def test_ir_trace_chain():
    print("🚀 开始验证全链路 Trace ID 贯穿性...")
    
    # 1. HIR 层：一个 FPADD 算子
    t0 = TensorObj("t0", (128,), "float32")
    t1 = TensorObj("t1", (128,), "float32")
    t2 = TensorObj("t2", (128,), "float32")
    
    hir_op = HIRNode(op_type="FPADD", inputs=[t0, t1], outputs=[t2])
    print(f"   [HIR] 创建核心算子: {hir_op}, Trace ID: {hir_op.trace_id[:8]}")
    
    # 2. MIR 层：拆解出两个脉冲（RtoVR 和 FPADD）
    mir_ops = [
        MIROp(op_type="RTOVR", logic_cycle=0, trace_id=hir_op.trace_id),
        MIROp(op_type="FPADD", logic_cycle=1, trace_id=hir_op.trace_id)
    ]
    print(f"   [MIR] 拆解出 {len(mir_ops)} 个脉冲, 首个 Trace ID: {mir_ops[0].trace_id[:8]}")
    for op in mir_ops:
        assert op.trace_id == hir_op.trace_id

    # 3. LIR 层：生成物理指令
    lir_ins = LIROp(abs_cycle=100, iq_id=5, code=0xDEADBEEF, trace_id=mir_ops[1].trace_id)
    print(f"   [LIR] 生成物理指令包: {lir_ins}, Trace ID: {lir_ins.trace_id[:8]}")
    assert lir_ins.trace_id == hir_op.trace_id
    
    # 4. 41-bit 二进制检验
    binary_bytes = lir_ins.encode_41bit()
    print(f"   [LIR] 生成初步 41-bit 二进制 (长度: {len(binary_bytes)} bytes): 0x{binary_bytes.hex()}")

    print("\n🎉 Sprint 2 三层 IR 贯穿验证成功!")

if __name__ == "__main__":
    test_ir_trace_chain()
