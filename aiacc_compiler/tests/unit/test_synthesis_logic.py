import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from ir.mir.mir_op import MIROp
from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer

def test_bank_rr_conflict_serialization():
    print("🚀 验证 SMT 是否自动处理 Bank R-R 冲突...")
    
    spec = HardwareSpec.load("hw_config")
    synthesizer = SMTSynthesizer(spec)
    
    # 模拟两个 RtoVR 操作，都读取 Bank 0
    # 它们共享同一个 IQ (IQ_RTOVR_5)
    op1 = MIROp(op_type="RTOVR_5", logic_cycle=0, trace_id="1")
    op1.metadata = {"iq_id": 5, "read_banks": [0]}
    
    op2 = MIROp(op_type="RTOVR_5", logic_cycle=0, trace_id="2")
    op2.metadata = {"iq_id": 5, "read_banks": [0]}
    
    # 求解
    result = synthesizer.solve([op1, op2])
    
    if result:
        t1, t2 = result[op1.node_id], result[op2.node_id]
        print(f"   [SAT] T1={t1}, T2={t2}")
        assert t1 != t2, "❌ R-R 冲突未自动错开拍数!"
    else:
        print("❌ SMT 求解出错 (UNSAT)")

def test_latency_propagation():
    print("🚀 验证 SMT 是否依据 Latency 自动排布时序...")
    
    spec = HardwareSpec.load("hw_config")
    synthesizer = SMTSynthesizer(spec)
    
    op_add = MIROp("FPADD", 0, "1")
    op_add.metadata = {"iq_id": 10} # 设 FPADD 占用 IQ 10
    
    op_next = MIROp("FPMUL", 1, "2")
    op_next.metadata = {"iq_id": 11, "depends_on": [op_add.node_id], "dep_latency": 4} # 依赖于 ADD
    
    result = synthesizer.solve([op_add, op_next])
    
    if result:
        t_add, t_next = result[op_add.node_id], result[op_next.node_id]
        print(f"   [SAT] T_ADD={t_add}, T_NEXT={t_next}")
        assert t_next >= t_add + 4, f"❌ Latency 约束未满足: {t_next} < {t_add} + 4"
    else:
        print("❌ SMT 求解出错 (UNSAT)")

if __name__ == "__main__":
    test_bank_rr_conflict_serialization()
    test_latency_propagation()
