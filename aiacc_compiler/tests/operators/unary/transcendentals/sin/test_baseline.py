import pytest
import numpy as np
from library.atomic.transcendentals.sin import get_sin_8order_dfg
from api.op_tester import OperatorPhysicalTester

class TestSinPhysicalIndustrialized:
    """
    [Standard SOP] 使用工业化测座 v6.6 进行 Sin 算子单点物理验证。
    """
    def test_sin_parity_baseline(self):
        # 1. 初始化测试基座 (统一规格)
        tester = OperatorPhysicalTester()
        
        # 2. 生成 8 阶泰勒 DFG
        dfg_ops = get_sin_8order_dfg()
        
        # 3. 准备物理测试数据 (4 路输入: pi/2, 0, pi/4, pi)
        x = np.array([1.570796, 0.0, 0.785398, -0.785398], dtype=np.float32)
        y_ref = np.sin(x).astype(np.float32)
        
        # 泰勒级数常量表 (严格对齐 8 阶展开系数)
        sin_constants = {
            "c1_rd": 1.984127e-4, 
            "c2_rd": 8.333333e-3, 
            "c3_rd": 1.666667e-1, 
            "c4_rd": 1.0,          
            "w_y": (x**2).astype(np.float32)        
        }
        
        # 4. 执行全自动物理验证 (向 build/sin/ 归档)
        success, msg = tester.run_full_stack_verification(
            op_name="Sin",
            dfg_ops=dfg_ops,
            input_data={"x_rd": x, "x_f_rd": x},
            golden_y=y_ref,
            constants=sin_constants,
            strategies=["balanced"], # 走标准平衡策略
            max_cycles=120
        )
        
        assert success, f"Sin 物理对齐测试失败: {msg}"
        print(f"\n✅ Sin 工业化分层验证 (v6.6) PASSED")

if __name__ == "__main__":
    pytest.main([__file__, "-s"])
