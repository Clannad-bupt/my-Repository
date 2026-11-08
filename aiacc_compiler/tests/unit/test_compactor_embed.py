import pytest
from driver.compactor import Compactor
from api.hw_spec import IQConfig

@pytest.fixture
def ur_read_iq():
    return IQConfig(
        module="UR_READ",
        EN_LOOPS=True,
        BITS_LOOPS=8,
        EN_INC=True,
        BITS_INC=4,
        BITS_CODE=12,
        BITS_EMBED=3
    )

def test_2d_block_folding(ur_read_iq):
    """
    测试 2D Tiling 访存折叠 (Header-Body 对验证)。
    """
    stream = []
    for row in range(2):
        for col in range(8):
            t = row * 8 + col
            payload = (1 << 11) | (0 << 8) | (row * 16 + col)
            stream.append((t, payload, False))
            
    folded = Compactor.compress_iq_stream(stream, ur_read_iq)
    
    # 期望: 1 条 Header + 1 条 Body
    assert len(folded) == 2
    
    # 1. 验证 Header (EMBED 1)
    header = folded[0]
    assert header["embed"] == 1
    assert header["loops"] == 1  # 2行 = 1次重复
    assert header["inc"] == 16   # 跨行 Stride
    
    # 2. 验证 Body (EMBED 0)
    body = folded[1]
    assert body["embed"] == 0
    assert body["loops"] == 7    # 8列 = 7次重复
    assert body["inc"] == 1      # 跨列 Stride (INC)
    assert body["embed_end"] == 1 # 标记可以闭合本层循环
