import unittest
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg
from orchestrator.factory import FootprintFactory

class TestHSTSStitcher(unittest.TestCase):
    def test_sin_parallel_stitching(self):
        """
        验证两个 sin 算子通过 HSTS 缝合是否能发现冲突。
        """
        spec = HardwareSpec.load("hw_config")
        synthesizer = SMTSynthesizer(spec)
        ops = get_sin_8order_dfg()
        
        # 1. 合成第一个 sin 指令流并转化为 FootprintMask
        result = synthesizer.solve(ops, strategy="extreme")
        self.assertIsNotNone(result)
        mask_master = FootprintFactory.from_smt_result(ops, result)
        
        # 2. 模拟第二个 sin 指令流 (相同掩码)
        mask_slave = FootprintFactory.from_smt_result(ops, result)
        
        # 3. 冲突检测：offset=0 (绝对冲突)
        self.assertTrue(mask_master.conflicts_with(mask_slave, offset=0), 
                        "Two sin instances at offset 0 MUST conflict on IQ channels.")
        
        # 4. 搜索缝合点 (Phase Shift Search)
        # 寻找第一个可以无冲突交错的起始拍数
        first_safe_offset = -1
        for offset in range(1, 10):
             if not mask_master.conflicts_with(mask_slave, offset=offset):
                 first_safe_offset = offset
                 break
        
        print(f"HSTS Found safe stitching offset for sin+sin: {first_safe_offset}")
        self.assertGreater(first_safe_offset, 0)
        
if __name__ == "__main__":
    unittest.main()
