"""
sim/tests/test_g2_raw_latch.py
================================
Group 2：RAW 依赖约束 + 动态锁存窗口
覆盖：H1（下界+上界）、I1（anti_overwrite）
"""
import unittest
import z3
from helpers import MockSpec, make_op
from synthesis.axioms import SynthesisAxioms


class TestRawAndLatchWindow(unittest.TestCase):

    def _sat(self, *constraints):
        s = z3.Solver()
        for c in constraints: s.add(c)
        return s.check() == z3.sat

    # ── 2-1 ─────────────────────────────────────────────────────────
    def test_raw_lower_bound_sat(self):
        """[H1-1] Consumer 发射 = prod + lat (因果满足) -> SAT"""
        tp, tc = z3.Int("tp"), z3.Int("tc")
        lat = 4
        self.assertTrue(self._sat(tp == 0, tc == lat, tc >= tp + lat))

    # ── 2-2 ─────────────────────────────────────────────────────────
    def test_raw_lower_bound_unsat(self):
        """[H1-2] Consumer 发射 < prod + lat (违背因果) -> UNSAT"""
        tp, tc = z3.Int("tp"), z3.Int("tc")
        lat = 4
        self.assertFalse(self._sat(tp == 0, tc == lat - 1, tc >= tp + lat))

    # ── 2-3 ─────────────────────────────────────────────────────────
    def test_latch_window_upper_sat_edge(self):
        """[H1-3] Consumer 在窗口上边界 (prod + lat + II - 1) 发射 -> SAT"""
        tp, tc = z3.Int("tp"), z3.Int("tc")
        lat, II = 4, 4
        self.assertTrue(self._sat(
            tp == 0, tc == lat + II - 1,
            tc >= tp + lat, tc <= tp + lat + II - 1
        ))

    # ── 2-4 ─────────────────────────────────────────────────────────
    def test_latch_window_upper_unsat_beyond(self):
        """[H1-4] Consumer 超出窗口 (被下一次迭代覆盖) -> UNSAT"""
        tp, tc = z3.Int("tp"), z3.Int("tc")
        lat, II = 4, 4
        self.assertFalse(self._sat(
            tp == 0, tc == lat + II,
            tc >= tp + lat, tc <= tp + lat + II - 1
        ))

    # ── 2-5 ─────────────────────────────────────────────────────────
    def test_anti_overwrite_enforcement(self):
        """
        [I1-1] 验证 anti_overwrite 正确拦截物理覆盖。
        当消费者滞后超出 II 窗口时，必须 UNSAT。
        """
        lat, II = 4, 1
        tp, tc = z3.Int("tp"), z3.Int("tc")
        solver = z3.Solver()
        solver.add(tp == 0, tc == lat + II, tc >= tp + lat)
        
        op1 = make_op("FPMUL", "prod")
        op2 = make_op("RTOVR", "cons", deps=["prod"])
        spec = MockSpec(instances={}, delays={"FPMUL": lat})
        
        # 启用 no_output_fifo=True
        SynthesisAxioms.add_anti_overwrite_axiom(
            solver, [op1, op2], {"prod": tp, "cons": tc}, II, spec,
            no_output_fifo=True
        )
        self.assertEqual(solver.check(), z3.unsat, "超窗调度应被拦截")


if __name__ == "__main__":
    unittest.main()
