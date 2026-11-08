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

# 批量导出工业级算子
from library.atomic.transcendentals.sin_cos_exp import get_sin_industrial_dfg, get_exp_industrial_dfg
from library.atomic.transcendentals.ln_tanh import get_ln_8order_dfg, get_tanh_dfg
from library.atomic.powers.precision_roots import get_rsqrt_double_newton_dfg
from library.atomic.arithmetic.basic_ops import get_add_dfg, get_mul_dfg
from library.atomic.logic.logic_ops import get_relu_dfg, get_square_dfg

class TestIndustrialBatch1Stress(unittest.TestCase):
    def test_batch1_industrial_stitching(self):
        spec = HardwareSpec.load("hw_config")
        synthesizer = SMTSynthesizer(spec)
        stitcher = HSTSStitcher()
        hw = HardwareSpec.load("hw_config")
        mem_alloc = MemoryAllocator(hw.params)
        
        # 1. 组建 10 算子工业级混合负载
        workload = [
            ("Sin_Ind", get_sin_industrial_dfg()), 
            ("Exp_Ind", get_exp_industrial_dfg()), 
            ("Ln_Ind", get_ln_8order_dfg()),
            ("Tanh_Ind", get_tanh_dfg()), 
            ("Rsqrt_Ind", get_rsqrt_double_newton_dfg()),
            ("Add", get_add_dfg()), 
            ("Mul", get_mul_dfg()),
            ("Square", get_square_dfg()), 
            ("Relu", get_relu_dfg())
        ]
        
        print(f"\n[INDUSTRIAL STRESS] Batch 1 (9-Ops Industrial Grade)...")
        
        for name, ops in workload:
            balanced_ops = ops
            res = synthesizer.solve(balanced_ops, "extreme")
            self.assertIsNotNone(res, f"Synthesis failed for {name}")
            offset = stitcher.add_operator(name, balanced_ops, res)
            print(f"  > Joined [{name:<10}] at Offset: {offset}")
            
        final_stream = stitcher.get_final_stream()
        for op in final_stream:
            if op.op_type in ["UR_READ", "UR_WRITE"]:
                is_c = op.node_id.startswith("c") or "tc" in op.node_id or "ec" in op.node_id
                op.metadata["p_addr"] = 0

        build_dir = Path(__file__).parent.parent.parent / "build" / "test" / "batch1_industrial"
        os.makedirs(build_dir, exist_ok=True)
        spec_file = str(build_dir / "batch1_industrial.precision.spec")
        HSTSSpecRenderer.render_to_file("batch1_industrial", final_stream, spec_file)
        
        last_offset = max((op.metadata["global_t"] - op.metadata["t"] for op in final_stream))
        avg_ph = last_offset / (len(workload)-1)
        print(f"\n[STRESS] Industrial Batch 1 COMPLETED. Avg Phase Shift: {avg_ph:.2f} cycles/op")
        print(f"✅ Industrial Spec Solidified at: {spec_file}")

if __name__ == "__main__":
    unittest.main()
