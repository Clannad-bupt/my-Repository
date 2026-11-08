import pytest
from orchestrator.stitcher import HSTSStitcher

class MockOp:
    def __init__(self, node_id, op_type, iq_id, bank_id):
        self.node_id = node_id
        self.op_type = op_type
        self.metadata = {
            "iq_id": iq_id,
            "bank_id": bank_id
        }

def test_hsts_raw_dependency():
    """测试具有 RAW 依赖的两个算子缝合"""
    stitcher = HSTSStitcher()
    
    # 算子 1: Add (T=0 到 10)
    # 假设它在 T=10 写入 Tensor_1
    stream_add = [MockOp("add_1", "FPADD", 0, 0)]
    res_add = {"add_1": 0} # 假设它就在 T=0
    # 我们模拟 Add 最终在 T=10 写入 (实际上通过分析 IQ 确定)
    
    # 算子 2: ReLU (T=0)
    # 它依赖 Tensor_1，所以它必须至少在 T=11 开始
    stream_relu = [MockOp("relu_1", "FPMUL", 1, 0)]
    res_relu = {"relu_1": 0}
    
    # 执行缝合，并传入前驱算子的结束时间约束
    offset_add = stitcher.add_operator("ADD", stream_add, res_add)
    # 我们通过 dependency 参数告诉 stitcher：RELU 必须在 ADD 之后
    # 假设约束是：Offset >= (Predecessor_Offset + Max_Local_T + 1)
    offset_relu = stitcher.add_operator("RELU", stream_relu, res_relu, depends_on=["ADD_0"])
    
    assert offset_add == 0
    # 即使 IQ 1 和 IQ 0 不冲突，ReLU 也必须因为数据依赖被推后到 1 (在这个简化测试中)
    # 如果 Add 在 T=0 指令后还有写延迟，对齐会更靠后
    assert offset_relu > offset_add
