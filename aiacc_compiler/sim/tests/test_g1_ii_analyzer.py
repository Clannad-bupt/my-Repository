"""
sim/tests/test_g1_ii_analyzer.py
================================
Group 1：IIAnalyzer 理论对齐验证
覆盖：A1（资源约束计算 + Bank 冲突下界分析）
"""
import unittest
from helpers import MockSpec, make_op
from core.ii_analyzer import IIAnalyzer


class TestIIAnalyzer(unittest.TestCase):

    # ── 1-1 ─────────────────────────────────────────────────────────
    def test_mac_mii_is_1(self):
        """
        [A1-1] a*b+c DFG
        期望：资源公式 II_res = ceil(usage/instances)
        实际 MII = 1。
        """
        spec = MockSpec(
            instances={"FPMUL": 1, "FPADD": 1, "UR_READ": 4, "UR_WRITE": 4},
            delays={"FPMUL": 4, "FPADD": 4, "UR_READ": 1, "UR_WRITE": 1},
        )
        ops = [
            make_op("UR_READ", "ra"), make_op("UR_READ", "rb"),
            make_op("UR_READ", "rc"), make_op("FPMUL", "mul"),
            make_op("FPADD", "add"), make_op("UR_WRITE", "w"),
        ]
        mii = IIAnalyzer(spec).compute(ops)
        self.assertEqual(mii, 1)

    # ── 1-2 ─────────────────────────────────────────────────────────
    def test_two_fpmul_one_instance_mii_2(self):
        """
        [A1-2] 2×FPMUL，单实例：II_res = 2
        """
        spec = MockSpec(instances={"FPMUL": 1}, delays={"FPMUL": 4})
        ops = [make_op("FPMUL", "m1"), make_op("FPMUL", "m2")]
        mii = IIAnalyzer(spec).compute(ops)
        self.assertEqual(mii, 2)

    # ── 1-3 ─────────────────────────────────────────────────────────
    def test_two_fpmul_two_instances_mii_1(self):
        """
        [A1-3] 2×FPMUL，双实例：II_res = 1
        """
        spec = MockSpec(instances={"FPMUL": 2}, delays={"FPMUL": 4})
        ops = [make_op("FPMUL", "m1"), make_op("FPMUL", "m2")]
        mii = IIAnalyzer(spec).compute(ops)
        self.assertEqual(mii, 1)

    # ── 1-4 ─────────────────────────────────────────────────────────
    def test_ii_bank_bound_dominates(self):
        """
        [A1-4] 3×UR_READ 全访问 bank 7。
        II_res = 1，但 II_bank = 3。
        正确 MII = max(1, 3) = 3。
        """
        spec = MockSpec(instances={"UR_READ": 4}, delays={"UR_READ": 1})
        ops = [
            make_op("UR_READ", "r1", bank_id=7),
            make_op("UR_READ", "r2", bank_id=7),
            make_op("UR_READ", "r3", bank_id=7),
        ]
        mii = IIAnalyzer(spec).compute(ops)
        self.assertEqual(mii, 3)

    # ── 1-5 ─────────────────────────────────────────────────────────
    def test_ii_bank_mixed_banks(self):
        """
        [A1-5] bank0×2, bank1×1, bank7×4。
        II_bank = max(2, 1, 4) = 4。
        """
        spec = MockSpec(instances={"UR_READ": 4}, delays={"UR_READ": 1})
        ops = [
            make_op("UR_READ", "r0", bank_id=0), make_op("UR_READ", "r1", bank_id=0),
            make_op("UR_READ", "r2", bank_id=1),
            make_op("UR_READ", "r3", bank_id=7), make_op("UR_READ", "r4", bank_id=7),
            make_op("UR_READ", "r5", bank_id=7), make_op("UR_READ", "r6", bank_id=7),
        ]
        mii = IIAnalyzer(spec).compute(ops)
        self.assertEqual(mii, 4)


if __name__ == "__main__":
    unittest.main()
