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
        BITS_CODE=12
    )

def test_ur_address_folding(ur_read_iq):
    """
    测试 UR_READ 连续地址折叠。
    Addr: 0, 1, 2, ..., 15
    预期: 1 条指令, LOOPS=15, INC=1, Base=0
    """
    # 构造原始流: (time, payload)
    # UR_READ payload 包含 [re:1, bank:3, addr:8]
    # 我们假设 addr 在低比特
    stream = []
    for t in range(16):
        payload = (1 << 11) | (0 << 8) | t  # RE=1, Bank=0, Addr=t
        stream.append((t, payload, False))
        
    folded = Compactor.compress_iq_stream(stream, ur_read_iq)
    
    assert len(folded) == 1
    assert folded[0]["loops"] == 15
    assert folded[0]["inc"] == 1
    assert folded[0]["payload"] == (1 << 11) | (0 << 8) | 0 # Base address

def test_ur_stride_folding(ur_read_iq):
    """
    测试 Stride=2 的访存折叠。
    Addr: 0, 2, 4, ..., 14
    预期: 1 条指令, LOOPS=7, INC=2
    """
    stream = []
    for t in range(8):
        payload = (1 << 11) | (0 << 8) | (t * 2)
        stream.append((t, payload, False))
        
    folded = Compactor.compress_iq_stream(stream, ur_read_iq)
    
    assert len(folded) == 1
    assert folded[0]["loops"] == 7
    assert folded[0]["inc"] == 2
