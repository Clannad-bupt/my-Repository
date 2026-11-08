import numpy as np
from typing import Any

class MockProgram:
    """
    [ADPC V5.4] 硬件感知型模拟内存层。
    移除所有 256/8 等硬编码，实时根据 HardwareSpec 构建内存拓扑。
    """
    class MockAllocator:
        def __init__(self, vector_lanes: int, bank_num: int, bank_depth: int, dtype: Any):
            self.vector_lanes = vector_lanes
            self.bank_depth = bank_depth
            # 🛸 [V5.4 FIX] 动态初始化 Bank 几何结构与数据类型
            self.banks = {
                i: [np.zeros(vector_lanes, dtype=dtype) for _ in range(bank_depth)] 
                for i in range(bank_num)
            }
    def __init__(self, spec: Any, dtype: Any = np.float32):
        self.vector_width_bits = getattr(spec.params, "PARAM_SMC_VECTOR_WIDTH", 128)
        # 🛸 [V6.6 FIX] 使用 numpy.dtype.itemsize 动态获取数据类型的物理位宽，彻底拆除任何 FP32/FP16 精度硬编码
        self.element_bits = np.dtype(dtype).itemsize * 8
        
        self.vector_lanes = self.vector_width_bits // self.element_bits
        self.bank_num = getattr(spec.params, "PARAM_UR_BANK_NUM", 8)
        self.bank_depth = getattr(spec.params, "PARAM_UR_BANK_DEPTH", 256)
        
        self.memory_allocator = self.MockAllocator(
            self.vector_lanes, self.bank_num, self.bank_depth, dtype
        )
        self.tensors = {}
        
    def inject_vector(self, logical_addr: int, data: np.ndarray, stride: int = 1):
        """注入矢量数据"""
        # 🛸 [V5.4 FIX] 使用动态 bank_depth 进行地址转换
        bank_id = logical_addr // self.bank_depth
        offset = logical_addr % self.bank_depth
        v_lanes = self.vector_lanes
        num_rows = (len(data) + v_lanes - 1) // v_lanes
        for r in range(num_rows):
            row_data = data[r*v_lanes : (r+1)*v_lanes]
            if len(row_data) < v_lanes:
                row_data = np.pad(row_data, (0, v_lanes - len(row_data)))
            self.memory_allocator.banks[bank_id][offset + r * stride] = row_data

    def inject_broadcast(self, logical_addr: int, scalar: float):
        """将标量广播到整个 SIMD 行"""
        bank_id = logical_addr // self.bank_depth
        offset = logical_addr % self.bank_depth
        self.memory_allocator.banks[bank_id][offset] = np.full(
            self.vector_lanes, scalar, dtype=self.memory_allocator.banks[0][0].dtype
        )
