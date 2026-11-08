import unittest
import copy
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg
from orchestrator.stitcher import HSTSStitcher

import os
from orchestrator.spec_renderer import HSTSSpecRenderer

class TestRobustStitcher(unittest.TestCase):
    def test_triple_sin_stitching(self):
        spec = HardwareSpec.load("hw_config")
        synthesizer = SMTSynthesizer(spec)
        stitcher = HSTSStitcher()
        
        ops = get_sin_8order_dfg()
        smt_res = synthesizer.solve(ops, strategy="extreme")
        self.assertIsNotNone(smt_res)
        
        stitcher.add_operator("sin_A", ops, smt_res)
        stitcher.add_operator("sin_B", get_sin_8order_dfg(), smt_res)
        stitcher.add_operator("sin_C", get_sin_8order_dfg(), smt_res)
        
        final_stream = stitcher.get_final_stream()
        
        # 将规格书产出重定向至 build 目录
        build_dir = Path(__file__).parent.parent.parent / "build" / "test" / "sin_stitched_3x"
        os.makedirs(build_dir, exist_ok=True)
        spec_path = str(build_dir / "sin_stitched_3x.gapless.spec")
        
        HSTSSpecRenderer.render_to_file("sin_stitched_3x", final_stream, spec_path)
        print(f"\n[BUILD] Specification solidify: {spec_path}")

if __name__ == "__main__":
    unittest.main()
