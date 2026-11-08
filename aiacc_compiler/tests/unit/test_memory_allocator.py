import pytest
from orchestrator.memory_allocator import MemoryAllocator
from api.hw_spec import HardwareParams

class MockTensor:
    def __init__(self, name, size, bank_id, start_t, end_t):
        self.name = name
        self.size = size
        self.bank_id = bank_id
        self.start_t = start_t
        self.end_t = end_t
        self.base_addr = None
        
    def __repr__(self):
        return f"T[{self.name}](B{self.bank_id}, Sz:{self.size}, Life:{self.start_t}-{self.end_t})"

@pytest.fixture
def hw_params():
    return HardwareParams(
        PARAM_SMC_NUM=32,
        PARAM_SMC_VECTOR_WIDTH=128,
        PARAM_IQ_DEPTH=256,
        PARAM_OUTSTANDING_INST_NUM=4,
        PARAM_EMBED_MAX_LEVEL=6,
        PARAM_SMC_AXI_EN=1,
        PARAM_UR_BANK_NUM=8,
        PARAM_UR_BANK_DEPTH=256
    )

def test_memory_basic_allocation(hw_params):
    """测试基本的 Bank 地址分配，不涉及复用"""
    tensors = [
        MockTensor("A", 64, 0, 0, 10),
        MockTensor("B", 128, 0, 0, 10)
    ]
    
    allocator = MemoryAllocator(hw_params)
    results = allocator.allocate(tensors)
    
    # 期望: A 和 B 互斥，不应重叠
    addr_a = results["A"]
    addr_b = results["B"]
    
    ranges = [(addr_a, addr_a + 64), (addr_b, addr_b + 128)]
    ranges.sort()
    
    # 检查重叠
    assert ranges[0][1] <= ranges[1][0]
    # 检查边界
    assert ranges[1][1] <= 256

def test_memory_reuse_logic(hw_params):
    """测试生命周期不重叠时的内存复用"""
    tensors = [
        MockTensor("A", 64, 0, 0, 10),
        MockTensor("B", 64, 0, 11, 20) 
    ]
    
    allocator = MemoryAllocator(hw_params)
    results = allocator.allocate(tensors)
    
    # 期望: 至少有一个能拿到 0 (因为贪婪算法会尝试 0)
    assert results["A"] == 0
    assert results["B"] == 0

def test_memory_overflow_guard(hw_params):
    """测试 Bank 深度溢出报错"""
    tensors = [
        MockTensor("A", 200, 0, 0, 10),
        MockTensor("B", 100, 0, 0, 10)
    ]
    
    allocator = MemoryAllocator(hw_params)
    with pytest.raises(MemoryError):
        allocator.allocate(tensors)
