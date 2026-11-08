"""
sim/tests/test_g3_bank_mutex.py
================================
Group 3：Bank 冲突与资源互斥
覆盖：H2（多实例符号化互斥）、H3（Bank 冲突）
"""
import unittest
import z3
from helpers import MockSpec, make_op
from synthesis.axioms import SynthesisAxioms


class TestBankAndMutex(unittest.TestCase):

    def setUp(self):
        self.solver = z3.Solver()
        # Mock T-vars
        self.t = {
            "op1": z3.Int("t_op1"), "op2": z3.Int("t_op2"),
            "op3": z3.Int("t_op3")
        }
        self.s = {
            "op1": z3.Int("s_op1"), "op2": z3.Int("s_op2"),
            "op3": z3.Int("s_op3")
        }

    # ── 3-1 ─────────────────────────────────────────────────────────
    def test_bank_conflict_same_bank_same_slot_unsat(self):
        """[H3-1] 同一 Bank 读，同一 Slot -> UNSAT"""
        ops = [make_op("UR_READ", "op1", bank_id=7), 
               make_op("UR_READ", "op2", bank_id=7)]
        self.solver.add(self.s["op1"] == 0, self.s["op2"] == 0)
        
        SynthesisAxioms.add_bank_conflict_axiom(self.solver, ops, self.t, self.s)
        self.assertEqual(self.solver.check(), z3.unsat)

    # ── 3-2 ─────────────────────────────────────────────────────────
    def test_bank_conflict_diff_bank_same_slot_sat(self):
        """[H3-2] 不同 Bank 读，同一 Slot -> SAT"""
        ops = [make_op("UR_READ", "op1", bank_id=0), 
               make_op("UR_READ", "op2", bank_id=7)]
        self.solver.add(self.s["op1"] == 0, self.s["op2"] == 0)
        
        SynthesisAxioms.add_bank_conflict_axiom(self.solver, ops, self.t, self.s)
        self.assertEqual(self.solver.check(), z3.sat)

    # ── 3-3 ─────────────────────────────────────────────────────────
    def test_multi_instance_mutex_same_iq_unsat(self):
        """[H2-1] 两个 FPMUL，被分配到同一个实例 (相同 IQ)，同一 Slot -> UNSAT"""
        ops = [make_op("FPMUL", "op1"), make_op("FPMUL", "op2")]
        iq_vars = {"op1": z3.Int("iq1"), "op2": z3.Int("iq2")}
        
        # 强制分给同一个物理实例，且同 slot
        self.solver.add(iq_vars["op1"] == 100, iq_vars["op2"] == 100)
        self.solver.add(self.s["op1"] == 2, self.s["op2"] == 2)
        
        spec = MockSpec(instances={"FPMUL": 2}, delays={"FPMUL": 4})
        SynthesisAxioms.add_alu_pipeline_axiom(self.solver, ops, self.t, iq_vars, spec, self.s)
        self.assertEqual(self.solver.check(), z3.unsat)

    # ── 3-4 ─────────────────────────────────────────────────────────
    def test_multi_instance_mutex_diff_iq_sat(self):
        """[H2-2] 两个 FPMUL，被分配到不同实例 (不同 IQ)，同一 Slot -> SAT"""
        ops = [make_op("FPMUL", "op1"), make_op("FPMUL", "op2")]
        iq_vars = {"op1": z3.Int("iq1"), "op2": z3.Int("iq2")}
        
        # 强制分给不同实例，同 slot
        self.solver.add(iq_vars["op1"] == 100, iq_vars["op2"] == 101)
        self.solver.add(self.s["op1"] == 2, self.s["op2"] == 2)
        
        spec = MockSpec(instances={"FPMUL": 2}, delays={"FPMUL": 4})
        SynthesisAxioms.add_alu_pipeline_axiom(self.solver, ops, self.t, iq_vars, spec, self.s)
        self.assertEqual(self.solver.check(), z3.sat)


if __name__ == "__main__":
    unittest.main()
