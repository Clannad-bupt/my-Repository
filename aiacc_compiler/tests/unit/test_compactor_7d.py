import pytest
from driver.compactor import Compactor
from api.hw_spec import IQConfig

@pytest.fixture
def ur_read_iq():
    return IQConfig(
        module="UR_READ",
        EN_LOOPS=True,
        BITS_LOOPS=10,
        EN_INC=True,
        BITS_INC=16, # 增大位宽以容纳大 Stride
        BITS_EMBED=3, 
        BITS_CODE=12
    )

def test_7d_shattered_nesting(ur_read_iq):
    """
    测试带维间断裂的 7D 折叠。
    这种模式下，1D 无法合并跨维数据。
    """
    stream = []
    # 模拟 7 层嵌套，每层 Stride 巨大，确保 1D 无法自愈
    strides = [1, 10, 100, 1000, 10000, 100000, 1000000]
    
    for d6 in range(2): 
        for d5 in range(2): 
            for d4 in range(2): 
                for d3 in range(2): 
                    for d2 in range(2): 
                        for d1 in range(2): 
                            for d0 in range(8): 
                                t = len(stream)
                                # 强制位错，产生 1D 断裂
                                addr = (d6*strides[6] + d5*strides[5] + d4*strides[4] + 
                                        d3*strides[3] + d2*strides[2] + d1*strides[1] + d0*strides[0])
                                payload = (1 << 30) | addr # 使用 30 位载荷模拟大地址
                                stream.append((t, payload, False))
                                
    folded = Compactor.compress_iq_stream(stream, ur_read_iq)
    
    print(f"7D Shattered Input: {len(stream)} -> Output: {len(folded)}")
    
    # 每一层应该精准嵌套
    # 最终应产出 7 条指令：EMBED 6, 5, 4, 3, 2, 1 加上最内层的 0
    assert len(folded) == 7
    levels = [f["embed"] for f in folded]
    assert levels == [6, 5, 4, 3, 2, 1, 0]
