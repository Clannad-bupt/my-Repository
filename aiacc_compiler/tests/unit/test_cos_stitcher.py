import unittest
import os
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.cos import get_cos_8order_dfg
from orchestrator.stitcher import HSTSStitcher
from orchestrator.spec_renderer import HSTSSpecRenderer

class TestCosStitcher(unittest.TestCase):
    def test_triple_cos_stitching(self):
        spec = HardwareSpec.load("hw_config")
        synthesizer = SMTSynthesizer(spec)
        stitcher = HSTSStitcher()
        
        ops = get_cos_8order_dfg()
        # 求解单个 Cos 的物理最优解
        smt_res = synthesizer.solve(ops, strategy="extreme")
        self.assertIsNotNone(smt_res)
        
        # 三联 Cos 串联
        stitcher.add_operator("cos_A", ops, smt_res)
        stitcher.add_operator("cos_B", get_cos_8order_dfg(), smt_res)
        stitcher.add_operator("cos_C", get_cos_8order_dfg(), smt_res)
        
        final_stream = stitcher.get_final_stream()
        
        # 导出至 build/test
        root = Path(__file__).parent.parent.parent
        build_dir = root / "build" / "test" / "cos_stitched_3x"
        os.makedirs(build_dir, exist_ok=True)
        spec_path = str(build_dir / "cos_stitched_3x.gapless.spec")
        
        HSTSSpecRenderer.render_to_file("cos_stitched_3x", final_stream, spec_path)
        print(f"\n[STRESS TEST] Cos Specification solidified at: {spec_path}")
        
        # 验证偏移量
        # 获取各实例第一个脉冲的时间戳
        offsets = sorted(list(set(op.metadata["global_t"] - op.metadata["t"] for op in final_stream)))
        print(f"[STRESS TEST] Cos Phase Offsets: {offsets}")
        
        self.assertEqual(len(offsets), 3)

if __name__ == "__main__":
    unittest.main()
