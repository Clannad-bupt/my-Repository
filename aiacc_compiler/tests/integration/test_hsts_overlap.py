import pytest
from orchestrator.stitcher import HSTSStitcher

class MockOp:
    def __init__(self, node_id, op_type, iq_id, bank_id):
        self.node_id = node_id
        self.op_type = op_type
        self.metadata = {"iq_id": iq_id, "bank_id": bank_id}

def test_hsts_parallel_interleaving():
    """测试两个并行算子（不同 Bank 资源时）的极致指令交织"""
    stitcher = HSTSStitcher()
    
    # 算子 A: IQ 0, Bank 0
    stream_a = [
        MockOp("a0", "FPADD", 0, 0),
        MockOp("a2", "FPADD", 0, 0),
        MockOp("a4", "FPADD", 0, 0)
    ]
    res_a = {"a0": 0, "a2": 2, "a4": 4}
    
    # 算子 B: IQ 1, Bank 1 (与 A 隔离)
    stream_b = [
        MockOp("b0", "FPMUL", 1, 1),
        MockOp("b1", "FPMUL", 1, 1)
    ]
    res_b = {"b0": 0, "b1": 1}
    
    # 缝合
    offset_a = stitcher.add_operator("ADD", stream_a, res_a)
    offset_b = stitcher.add_operator("MUL", stream_b, res_b)
    
    # 期望：两者都从全局时间 T=0 开始
    assert offset_a == 0
    assert offset_b == 0
    
    final = stitcher.get_final_stream()
    times = [op.metadata["global_t"] for op in final]
    print(f"Isolated Parallel Interleaving Result: {times}")
    
    # 指令序列应表现为 [T=0(A), T=0(B), T=1(B), T=2(A), T=4(A)]
    counts = {t: times.count(t) for t in set(times)}
    assert counts[0] == 2 # T=0 时刻发射两条并行指令
    assert counts[1] == 1
