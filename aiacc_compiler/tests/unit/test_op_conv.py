import pytest
from library.structured.conv_gen import Conv2DGenerator
from driver.compactor import Compactor
from api.hw_spec import IQConfig

@pytest.fixture
def ur_read_iq():
    return IQConfig(
        module="UR_READ",
        EN_LOOPS=True, BITS_LOOPS=8,
        EN_INC=True, BITS_INC=16,
        BITS_EMBED=3, BITS_CODE=12
    )

def test_conv2d_multi_dim_folding(ur_read_iq):
    """
    测试 Conv2D 滑动窗口的 4D 自动化折叠性能。
    输入: 1x4x4, Kernel: 2x2, Output: 3x3
    共 36 次 UR_READ。
    """
    # 1. 产生线性物理流
    gen = Conv2DGenerator(in_shape=(1, 4, 4), kernel=(2, 2), stride=1)
    mock_op_list = gen.generate_parallel_lir(ibank=0, wbank=1)
    
    # 格式转换: [(t, payload, is_rigid), ...] 为 Compactor 输入
    stream = [(op["t"], op["raddr"], False) for op in mock_op_list if op.get("iq_id") == 0]
    
    # 2. 调用 7D 极致压缩
    folded = Compactor.compress_iq_stream(stream, ur_read_iq)
    
    print(f"Conv2D Linear: {len(stream)} -> Folded: {len(folded)}")
    
    # 我们期望 36 条指令被浓缩为 4 层嵌套
    # EMBED 3 (OH), 2 (OW), 1 (KH), 指令 0 (KW) -> 最少应产出 4 条
    assert len(folded) <= 4
    
    # 校验指令层级
    levels = [f["embed"] for f in folded]
    assert 3 in levels  # OH
    assert 2 in levels  # OW
    assert 1 in levels  # KH
    assert 0 in levels  # KW
