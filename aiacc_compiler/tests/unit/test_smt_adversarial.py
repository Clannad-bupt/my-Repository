import unittest
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent.parent / "src"))

from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT
from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer

class TestSMTAdversarial(unittest.TestCase):
    def setUp(self):
        self.spec = HardwareSpec.load("hw_config")
        self.synthesizer = SMTSynthesizer(self.spec)

    def test_fpmul_starvation(self):
        """[用例 3] 算力饥饿：13 个无依赖 FPMUL，硬件只有 4 个，逼出 II >= 4 的鸽巢极限"""
        b = MIROpBuilder()
        for i in range(13):
            b.make_op("FPMUL", f"m_{i}", RT.FPMUL, [], f"M{i}")
        ops = b.get_dfg()
        
        res = self.synthesizer.solve(ops, strategy="extreme")
        self.assertIsNotNone(res)
        self.assertGreaterEqual(res["II"], 4)

    def test_bank_meatgrinder(self):
        """[用例 4] 内存绞肉机：9 个并发读操作死磕 Bank 2，逼出 II >= 9"""
        b = MIROpBuilder()
        for i in range(9):
            b.make_op("UR_READ", f"r_{i}", RT.UR_READ, [], f"R{i}")
        ops = b.get_dfg()
        for op in ops:
            op.metadata["manual_bank"] = 2
        
        res = self.synthesizer.solve(ops, strategy="extreme")
        self.assertIsNotNone(res)
        self.assertGreaterEqual(res["II"], 9)

    def test_asymmetric_fork_join(self):
        """[用例 5] 非对称生命周期陷阱 (Zero-FIFO Latch Guardian)
        证明 SMT 的 O(E) 上界能完美拦截跨周期的 Latch 覆盖！
        """
        b = MIROpBuilder()
        
        # 节点 A 产生数据 (FPADD 的 lat=4)
        a = b.make_op("FPADD", "a", RT.FPADD, [], "A")
        
        # 深渊延迟链：C1 依赖 A，C2 依赖 C1... C4 依赖 C3
        curr = b.make_op("FPADD", "c_1", RT.FPADD, [a], "C1")
        for i in range(2, 5):
            curr = b.make_op("FPADD", f"c_{i}", RT.FPADD, [curr], f"C{i}")
            
        # 终点 D，既依赖 A（跨越漫长岁月），又依赖 C4
        d = b.make_op("FPADD", "d", RT.FPADD, [a, curr], "D")
        
        ops = b.get_dfg()
        
        # 【数学推导】
        # t[c_1] >= t[a] + 4
        # t[c_2] >= t[c_1] + 4 >= t[a] + 8
        # t[c_3] >= t[c_2] + 4 >= t[a] + 12
        # t[c_4] >= t[c_3] + 4 >= t[a] + 16
        # t[d] >= t[c_4] + 4 >= t[a] + 20
        # 
        # 节点 D 此时才来读 A，为了保证 A 的 Latch 不被下一次循环的自己覆盖：
        # t[d] < t[a] + lat_a(4) + II
        # 20 < 4 + II  =>  II > 16  =>  II >= 17
        
        res = self.synthesizer.solve(ops, strategy="extreme")
        self.assertIsNotNone(res)
        self.assertGreaterEqual(res["II"], 17)

class TestSMTRefutation(unittest.TestCase):
    """
    🔴 反证测试套件 (Refutation / UNSAT Tests)
    
    正向测试验证"SMT 解出的结果是否正确"，
    反证测试验证"当物理上绝对不可能时，SMT 是否正确拒绝而非幻觉出假解"。
    
    一个总是返回 SAT 的求解器同样能通过所有正向测试，但会在硬件上彻底崩溃。
    """
    def setUp(self):
        self.spec = HardwareSpec.load("hw_config")
        self.synthesizer = SMTSynthesizer(self.spec)

    def test_impossible_ii_bank_conflict(self):
        """
        [反证 R1] 过约束系统：9 读操作挤 bank=2，同时强制 II=8
        物理下界是 II=9，因此 II=8 在数学上绝对 UNSAT。
        断言：SMT 必须返回 None（表示无解），而不是幻觉出一个假 SAT。
        """
        b = MIROpBuilder()
        for i in range(9):
            b.make_op("UR_READ", f"r_{i}", RT.UR_READ, [], f"R{i}")
        ops = b.get_dfg()
        for op in ops:
            op.metadata["manual_bank"] = 2

        # 直接调用内部的 _solve_for_ii，强制指定一个不可能的 II
        result = self.synthesizer._solve_for_ii(ops, II=8)
        self.assertIsNone(result,
            "SMT 对 II=8 不应产生解（9 个操作挤在同一 Bank，物理下界 II=9）。"
            "如果返回非 None，说明 Bank 冲突约束存在致命漏洞！")

    def test_impossible_ii_fpmul_starvation(self):
        """
        [反证 R2] 算力过约束：13 个 FPMUL，4 个硬件单元，强制 II=3
        鸽巢原理下界 II=ceil(13/4)=4，因此 II=3 绝对 UNSAT。
        """
        b = MIROpBuilder()
        for i in range(13):
            b.make_op("FPMUL", f"m_{i}", RT.FPMUL, [], f"M{i}")
        ops = b.get_dfg()

        result = self.synthesizer._solve_for_ii(ops, II=3)
        self.assertIsNone(result,
            "SMT 对 II=3 不应产生解（13 个 FPMUL，4 个单元，鸽巢下界 II=4）。"
            "如果返回非 None，说明资源互斥约束存在致命漏洞！")

    def test_impossible_ii_latch_lifetime(self):
        """
        [反证 R3] 生命周期过约束：非对称分叉图（数学下界 II=17），强制 II=16
        Zero-FIFO 生命周期上界要求 II >= 17，因此 II=16 绝对 UNSAT。
        """
        b = MIROpBuilder()
        a = b.make_op("FPADD", "a", RT.FPADD, [], "A")
        curr = b.make_op("FPADD", "c_1", RT.FPADD, [a], "C1")
        for i in range(2, 5):
            curr = b.make_op("FPADD", f"c_{i}", RT.FPADD, [curr], f"C{i}")
        b.make_op("FPADD", "d", RT.FPADD, [a, curr], "D")
        ops = b.get_dfg()

        result = self.synthesizer._solve_for_ii(ops, II=16)
        self.assertIsNone(result,
            "SMT 对 II=16 不应产生解（数学推导下界 II=17）。"
            "如果返回非 None，说明 O(E) Latch 生命周期上界约束存在致命漏洞！")

    def test_sat_just_above_impossible(self):
        """
        [反证 R4] 边界吻合验证：同一个图，II=17 必须是 SAT，II=16 必须是 UNSAT。
        验证求解器的 SAT/UNSAT 边界没有偏移（不能有 off-by-one 错误）。
        """
        b = MIROpBuilder()
        a = b.make_op("FPADD", "a", RT.FPADD, [], "A")
        curr = b.make_op("FPADD", "c_1", RT.FPADD, [a], "C1")
        for i in range(2, 5):
            curr = b.make_op("FPADD", f"c_{i}", RT.FPADD, [curr], f"C{i}")
        b.make_op("FPADD", "d", RT.FPADD, [a, curr], "D")
        ops = b.get_dfg()

        import copy
        result_16 = self.synthesizer._solve_for_ii(copy.deepcopy(ops), II=16)
        result_17 = self.synthesizer._solve_for_ii(copy.deepcopy(ops), II=17)

        self.assertIsNone(result_16, "II=16 必须 UNSAT（边界下方）")
        self.assertIsNotNone(result_17, "II=17 必须 SAT（精确边界）。若 UNSAT 则约束过严！")


if __name__ == "__main__":
    unittest.main()
