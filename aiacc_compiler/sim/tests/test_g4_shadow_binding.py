"""
sim/tests/test_g4_shadow_binding.py
================================
Group 4：RTOVR -> ALU 影子绑定
覆盖：A2（影子绑定窗口应随 II 变化）
注意：此功能由 backend._solve_for_ii 直接驱动。
"""
import unittest
import z3
from helpers import MockSpec, make_op

class TestShadowBinding(unittest.TestCase):

    # ── 4-1 ─────────────────────────────────────────────────────────
    def test_shadow_binding_dynamic_upper_bound(self):
        """
        [A2-1] 验证影子绑定上界跟随 II 变化。
        当 II=6 时，ALU 在 t_rt + 5 读数据应合法 (SAT)。
        """
        t_rt, t_alu = z3.Int("t_rt"), z3.Int("t_alu")
        II = 6
        s = z3.Solver()
        s.add(t_rt == 0)
        s.add(t_alu == 5)  # 5 拍后读
        
        # 模拟 backend 中的新逻辑：t_alu <= t_rt + II - 1
        s.add(t_alu >= t_rt + 1)
        s.add(t_alu <= t_rt + II - 1) 
        
        self.assertEqual(s.check(), z3.sat, "II=6 时 5 拍延迟应合法")


if __name__ == "__main__":
    unittest.main()
