import unittest
import os
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg
from library.atomic.transcendentals.cos import get_cos_8order_dfg
from library.atomic.transcendentals.exp import get_exp_8order_dfg
from library.atomic.powers.rsqrt import get_rsqrt_newton_dfg
from library.atomic.arithmetic.linear import get_linear_dfg
from orchestrator.stitcher import HSTSStitcher
from orchestrator.colorer import BalancedBankColorer
from orchestrator.memory_allocator import MemoryAllocator
from orchestrator.spec_renderer import HSTSSpecRenderer

class TestSuperHeavyHSTS(unittest.TestCase):
    def test_super_heavy_hybrid(self):
        spec = HardwareSpec.load("hw_config")
        synthesizer = SMTSynthesizer(spec)
        stitcher = HSTSStitcher()
        hw = HardwareSpec.load("hw_config")
        mem_alloc = MemoryAllocator(hw.params)
        
        # 1. 自动着色全套算子库
        workload = [
            ("Sin", get_sin_8order_dfg()),
            ("Rsqrt", get_rsqrt_newton_dfg()),
            ("Exp", get_exp_8order_dfg()),
            ("Linear", get_linear_dfg())
        ]
        
        # 2. 批量合成并缝合
        for name, ops in workload:
            balanced_ops = ops
            res = synthesizer.solve(balanced_ops, "extreme")
            self.assertIsNotNone(res, f"Synthesis failed for {name}")
            stitcher.add_operator(name, balanced_ops, res)
            
        # 3. 空间分配
        final_stream = stitcher.get_final_stream()
        for op in final_stream:
            if op.op_type in ["UR_READ", "UR_WRITE"]:
                is_c = op.node_id.startswith("c") or "mean" in op.node_id or "scale" in op.node_id
                op.metadata["p_addr"] = 0

        # 4. 导出规格书
        build_path = Path(__file__).parent.parent.parent / "build" / "test" / "heavy_hybrid"
        os.makedirs(build_path, exist_ok=True)
        spec_file = str(build_path / "heavy.precision.spec")
        HSTSSpecRenderer.render_to_file("heavy_hybrid", final_stream, spec_file)
        
        print(f"\n✅ Super Heavy Hybrid Spec Solidified: {spec_file}")
        pass

if __name__ == "__main__":
    unittest.main()
