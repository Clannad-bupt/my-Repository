import unittest
import os
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from orchestrator.stitcher import HSTSStitcher
from orchestrator.colorer import BalancedBankColorer
from orchestrator.memory_allocator import MemoryAllocator
from orchestrator.spec_renderer import HSTSSpecRenderer

# 修正导入路径
from library.atomic.transcendentals.sin import get_sin_8order_dfg
from library.atomic.transcendentals.cos import get_cos_8order_dfg
from library.atomic.transcendentals.exp import get_exp_8order_dfg
from library.atomic.transcendentals.ln_tanh import get_ln_8order_dfg, get_tanh_dfg
from library.atomic.powers.rsqrt import get_rsqrt_newton_dfg
from library.atomic.arithmetic.basic_ops import get_add_dfg, get_mul_dfg
from library.atomic.logic.logic_ops import get_relu_dfg, get_square_dfg

class TestBatch1Stress(unittest.TestCase):
    def test_batch1_hetero_stitching(self):
        spec = HardwareSpec.load("hw_config")
        synthesizer = SMTSynthesizer(spec)
        stitcher = HSTSStitcher()
        hw = HardwareSpec.load("hw_config")
        mem_alloc = MemoryAllocator(hw.params)
        
        # 1. 组建 10 算子混合负载
        workload = [
            ("Sin", get_sin_8order_dfg()), ("Cos", get_cos_8order_dfg()),
            ("Exp", get_exp_8order_dfg()), ("Ln", get_ln_8order_dfg()),
            ("Tanh", get_tanh_dfg()), ("Rsqrt", get_rsqrt_newton_dfg()),
            ("Add", get_add_dfg()), ("Mul", get_mul_dfg()),
            ("Square", get_square_dfg()), ("Relu", get_relu_dfg())
        ]
        
        print(f"\n[STRESS] Kickoff Batch 1 Test (10 Different Operators)...")
        
        for name, ops in workload:
            balanced_ops = ops
            res = synthesizer.solve(balanced_ops, "extreme")
            self.assertIsNotNone(res, f"Synthesis failed for {name}")
            offset = stitcher.add_operator(name, balanced_ops, res)
            print(f"  > Joined [{name:<10}] at Offset: {offset}")
            
        final_stream = stitcher.get_final_stream()
        for op in final_stream:
            if op.op_type in ["UR_READ", "UR_WRITE"]:
                is_c = op.node_id.startswith("c")
                op.metadata["p_addr"] = 0

        build_dir = Path(__file__).parent.parent.parent / "build" / "test" / "batch1_stress"
        os.makedirs(build_dir, exist_ok=True)
        spec_file = str(build_dir / "batch1.precision.spec")
        HSTSSpecRenderer.render_to_file("batch1_10ops", final_stream, spec_file)
        
        last_offset = max((op.metadata["global_t"] - op.metadata["t"] for op in final_stream))
        avg_ph = last_offset / (len(workload)-1)
        print(f"\n[STRESS] Batch 1 (10 Ops) COMPLETED. Avg Phase Shift: {avg_ph:.2f} cycles/op")
        print(f"✅ Spec Solidified at: {spec_file}")

if __name__ == "__main__":
    unittest.main()
