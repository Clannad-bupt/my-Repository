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

# 批量导出工业级算子 (Batch 1-4)
from library.atomic.transcendentals.sin_cos_exp import get_sin_industrial_dfg, get_exp_industrial_dfg
from library.atomic.transcendentals.ln_tanh import get_ln_8order_dfg, get_tanh_dfg
from library.atomic.powers.precision_roots import get_rsqrt_double_newton_dfg, get_sqrt_industrial_dfg, get_cbrt_industrial_dfg
from library.atomic.arithmetic.basic_ops import get_add_dfg, get_mul_dfg, get_div_industrial_dfg
from library.atomic.logic.precision_logic import get_max_industrial_dfg, get_clamp_industrial_dfg, get_greater_industrial_dfg
from library.atomic.rounding.precision_rounding import get_floor_industrial_dfg, get_mod_industrial_dfg
from library.atomic.trig_extra.precision_trig import get_atan2_industrial_dfg, get_asin_industrial_dfg
from library.atomic.transcendentals.hyperbolic import get_sinh_industrial_dfg, get_cosh_industrial_dfg
from library.atomic.logic.special_logic import get_isnan_industrial_dfg, get_isinf_industrial_dfg

class TestBatch4IndustrialStress(unittest.TestCase):
    def test_batch4_full_hybrid_stitching(self):
        spec = HardwareSpec.load("hw_config")
        synthesizer = SMTSynthesizer(spec)
        stitcher = HSTSStitcher()
        hw = HardwareSpec.load("hw_config")
        mem_alloc = MemoryAllocator(hw.params)
        
        # 1. 组建 14 算子(Batch 4 Focus) 工业级极限混合负载
        workload = [
            ("Sin", get_sin_industrial_dfg()), ("Exp", get_exp_industrial_dfg()), 
            ("Atan2", get_atan2_industrial_dfg()), ("Asin", get_asin_industrial_dfg()), 
            ("Sinh", get_sinh_industrial_dfg()), ("Cosh", get_cosh_industrial_dfg()),
            ("IsNan", get_isnan_industrial_dfg()), ("IsInf", get_isinf_industrial_dfg()),
            ("Div", get_div_industrial_dfg()), ("Add", get_add_dfg()), 
            ("Mul", get_mul_dfg()), ("Max", get_max_industrial_dfg()),
            ("Clamp", get_clamp_industrial_dfg()), ("Floor", get_floor_industrial_dfg())
        ]
        
        print(f"\n[INDUSTRIAL STRESS Batch 4] Kickoff (14-Ops Deep Hybrid Grade)...")
        
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
                        "ec" in op.node_id or "bias" in op.node_id or "pi" in op.node_id)
                op.metadata["p_addr"] = 0

        # 3. 产物固化
        build_dir = Path(__file__).parent.parent.parent / "build" / "test" / "batch4_industrial"
        os.makedirs(build_dir, exist_ok=True)
        spec_file = str(build_dir / "batch4.precision.spec")
        HSTSSpecRenderer.render_to_file("batch4_14ops", final_stream, spec_file)
        
        last_offset = max((op.metadata["global_t"] - op.metadata["t"] for op in final_stream))
        avg_ph = last_offset / (len(workload)-1)
        print(f"\n[STRESS] Industrial Batch 4 (14 Ops) COMPLETED. Avg Phase Shift: {avg_ph:.2f} cycles/op")
        print(f"✅ Spec Solidified at: {spec_file}")

if __name__ == "__main__":
    unittest.main()
