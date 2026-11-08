import unittest
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent.parent / "src"))

from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT
from api.hw_spec import HardwareSpec
from synthesis.backend import TimingVerifier

class TestVerifierRobustness(unittest.TestCase):
    def setUp(self):
        self.spec = HardwareSpec.load("hw_config")
        self.verifier = TimingVerifier(self.spec)

    def test_latch_expire_trap(self):
        """[用例 1] 假释期抓捕：强行把消费者读取时间设在数据被冲刷的时间"""
        b = MIROpBuilder()
        # lat_a = 4 (FPADD)
        op_a = b.make_op("FPADD", "a", RT.FPADD, [], "A")
        op_b = b.make_op("FPADD", "b", RT.FPADD, [op_a], "B")
        dfg = b.get_dfg()
        
        # II=5，A 的存活窗口是 [t_a+lat, t_a+lat+II-1] = [t_a+4, t_a+8]
        # 如果 t_b = 9，就会读取到被污染的 Latch 数据
        schedule = {
            op_a: 0,
            op_b: 9
        }
        with self.assertRaisesRegex(RuntimeError, "Latch Overwrite"):
            self.verifier.verify(dfg, schedule, II=5)

    def test_phantom_bank_conflict(self):
        """[用例 2] 幽灵撞车：让两个同 Bank 的读取操作在模槽中重叠"""
        b = MIROpBuilder()
        r1 = b.make_op("UR_READ", "r1", RT.UR_READ, [], "R1")
        r2 = b.make_op("UR_READ", "r2", RT.UR_READ, [], "R2")
        dfg = b.get_dfg()
        
        for op in dfg:
            if op.trace_id in [r1, r2]:
                op.metadata["manual_bank"] = 3
        
        # 虽然绝对时间不同，但模 16 之后都在第 2 槽位，触发 Bank 物理冲突
        schedule = {
            r1: 2,
            r2: 18 # 18 % 16 == 2
        }
        with self.assertRaisesRegex(RuntimeError, "Bank Conflict"):
            self.verifier.verify(dfg, schedule, II=16)

if __name__ == "__main__":
    unittest.main()
