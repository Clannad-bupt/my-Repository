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

# 批量导出工业算子 (Batch 1 + Batch 2)
from library.atomic.transcendentals.sin_cos_exp import get_sin_industrial_dfg, get_exp_industrial_dfg
from library.atomic.transcendentals.ln_tanh import get_ln_8order_dfg, get_tanh_dfg
from library.atomic.powers.precision_roots import get_rsqrt_double_newton_dfg, get_sqrt_industrial_dfg, get_cbrt_industrial_dfg
from library.atomic.arithmetic.basic_ops import get_add_dfg, get_mul_dfg
from library.atomic.logic.logic_ops import get_relu_dfg, get_square_dfg
from library.atomic.logic.precision_logic import get_max_industrial_dfg, get_clamp_industrial_dfg
from library.atomic.rounding.precision_rounding import get_floor_industrial_dfg, get_mod_industrial_dfg

class TestBatch2IndustrialStress(unittest.TestCase):
    def test_batch2_full_hybrid_stitching(self):
        spec = HardwareSpec.load("hw_config")
        synthesizer = SMTSynthesizer(spec)
        stitcher = HSTSStitcher()
        hw = HardwareSpec.load("hw_config")
        mem_alloc = MemoryAllocator(hw.params)
        
        # 1. 组建 14 算子(Batch 2) 工业级极限混合负载
        workload = [
            ("Sin", get_sin_industrial_dfg()), ("Exp", get_exp_industrial_dfg()), 
            ("Ln", get_ln_8order_dfg()), ("Tanh", get_tanh_dfg()), 
            ("Rsqrt", get_rsqrt_double_newton_dfg()), ("Sqrt", get_sqrt_industrial_dfg()),
            ("Cbrt", get_cbrt_industrial_dfg()), ("Add", get_add_dfg()), 
            ("Mul", get_mul_dfg()), ("Square", get_square_dfg()), 
            ("Relu", get_relu_dfg()), ("Max", get_max_industrial_dfg()),
            ("Clamp", get_clamp_industrial_dfg()), ("Floor", get_floor_industrial_dfg()),
            ("Ceil", get_mod_industrial_dfg())
        ]
        
        print(f"\n[INDUSTRIAL STRESS Batch 2] Kickoff (15-Ops Hybrid Grade)...")
        
        for name, ops in workload:
            balanced_ops = ops
            res = synthesizer.solve(balanced_ops, "extreme")
            self.assertIsNotNone(res, f"Synthesis failed for {name}")
            offset = stitcher.add_operator(name, balanced_ops, res)
            print(f"  > Joined [{name:<10}] at Offset: {offset}")
            
        final_stream = stitcher.get_final_stream()
        for op in final_stream:
            if op.op_type in ["UR_READ", "UR_WRITE"]:
                is_c = (op.node_id.startswith("c") or "tc" in op.node_id or 
                        "ec" in op.node_id or "bias" in op.node_id)
                op.metadata["p_addr"] = 0

        # 3. 产物固化
        build_dir = Path(__file__).parent.parent.parent / "build" / "test" / "batch2_industrial"
        os.makedirs(build_dir, exist_ok=True)
        spec_file = str(build_dir / "batch2.precision.spec")
        HSTSSpecRenderer.render_to_file("batch2_15ops", final_stream, spec_file)
        
        last_offset = max((op.metadata["global_t"] - op.metadata["t"] for op in final_stream))
        avg_ph = last_offset / (len(workload)-1)
        print(f"\n[STRESS] Industrial Batch 2 (15 Ops) COMPLETED. Avg Phase Shift: {avg_ph:.2f} cycles/op")
        print(f"✅ Spec Solidified at: {spec_file}")

if __name__ == "__main__":
    unittest.main()
