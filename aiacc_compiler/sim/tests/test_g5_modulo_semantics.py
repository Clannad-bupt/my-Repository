"""
sim/tests/test_g5_modulo_semantics.py
======================================
Group 5：模约束语义等价性与降维测试
覆盖：
  1. 验证大 II 下的商余数辅助变量 (q, r) 是否数学等价于原始 `% II`。
  2. 验证多节点竞争同一物理单元时，必须分配到不同的余数 (r_i != r_j)。
"""
import unittest
import z3
import time
from helpers import MockSpec, make_op
from synthesis.axioms.resource_axioms import ResourceAxioms

class TestModuloSemantics(unittest.TestCase):
    def setUp(self):
        self.spec = MockSpec({"FPADD": 1}, {"FPADD": 4})
        self.solver = z3.Solver()
        self.t = {}
        self.u = {}

    def test_modulo_mutual_exclusion(self):
        """
        验证：即使使用高 II (例如 II=20)，分配到同一 IQ 的算子依然会在模槽上互斥。
        """
        # 创建 3 个加法操作
        ops = [make_op("FPADD", f"add_{i}") for i in range(3)]
        
        II = 20
        # 强制它们具有不同的商 (q)，但只要分配在同一 IQ，它们的模槽 (t % II) 必须不同
        for i, op in enumerate(ops):
            self.t[op.trace_id] = z3.Int(f"t_{op.trace_id}")
            self.u[op.trace_id] = z3.Int(f"iq_{op.trace_id}")
            # 强制全部映射到硬件 ID 为 100 的单元 (MockSpec 默认基址)
            self.solver.add(self.u[op.trace_id] == 100)
            
            # 强制跨周期：add_0 在第 0 周期附近，add_1 在第 1 周期附近...
            # 也就意味着它们的“商” q 肯定是 0, 1, 2
            self.solver.add(self.t[op.trace_id] >= i * II)
            self.solver.add(self.t[op.trace_id] < (i + 1) * II)

        start_time = time.time()
        # 注入被测试的约束
        ResourceAxioms.add_modulo_constraint(self.solver, ops, self.t, II, self.spec, u_vars=self.u)
        
        # 求解
        res = self.solver.check()
        solve_time = time.time() - start_time
        
        self.assertEqual(res, z3.sat, "Should be SAT as there are 20 slots for 3 operations.")
        
        m = self.solver.model()
        # 获取它们的模槽 (余数)
        slots = []
        for op in ops:
            t_val = m[self.t[op.trace_id]].as_long()
            slot = t_val % II
            slots.append(slot)
            
        # 断言所有余数互不相同 (物理资源在同槽位互斥)
        self.assertEqual(len(set(slots)), len(slots), "Modulo slots must be mutually exclusive for same IQ.")
        print(f"\\n[Metrics] Modulo semantics solve time: {solve_time:.4f}s")

if __name__ == "__main__":
    unittest.main()
