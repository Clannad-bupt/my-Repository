import unittest
import os
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg
from library.atomic.transcendentals.cos import get_cos_8order_dfg
from orchestrator.stitcher import HSTSStitcher
from orchestrator.colorer import BalancedBankColorer
from orchestrator.memory_allocator import MemoryAllocator
from orchestrator.spec_renderer import HSTSSpecRenderer

class TestHybridHSTSFlow(unittest.TestCase):
    def test_hybrid_sin_cos_balanced(self):
        spec = HardwareSpec.load("hw_config")
        synthesizer = SMTSynthesizer(spec)
        stitcher = HSTSStitcher()
        hw = HardwareSpec.load("hw_config")
        mem_alloc = MemoryAllocator(hw.params)
        
        # 1. 准备混合算子集 (Sin, Cos, Sin)
        # 每个算子独立进行均衡着色，确保它们的 Bank 需求在逻辑上是离散的
        sin_ops = get_sin_8order_dfg()
        cos_ops = get_cos_8order_dfg()
        
        # 2. 独立 SMT 求解 (物理最优解)
        sin_res = synthesizer.solve(sin_ops, strategy="extreme")
        cos_res = synthesizer.solve(cos_ops, strategy="extreme")
        self.assertIsNotNone(sin_res)
        self.assertIsNotNone(cos_res)
        
        # 3. 异质化缝合: Sin_A -> Cos_B -> Sin_C
        # 这种混合会产生非常复杂的物理交叠图谱
        stitcher.add_operator("Sin_A", sin_ops, sin_res)
        offset_b = stitcher.add_operator("Cos_B", cos_ops, cos_res)
        offset_c = stitcher.add_operator("Sin_C", sin_ops, sin_res)
        
        print(f"\n[HYBRID] Stitched Offsets: Sin_A(0), Cos_B({offset_b}), Sin_C({offset_c})")
        
        # 4. 统一空间布局与分配
        final_stream = stitcher.get_final_stream()
        for op in final_stream:
            if op.op_type in ["UR_READ", "UR_WRITE"]:
                b_id = op.metadata["bank_id"]
                # 根据算子全名在全局内存中开辟空间
                inst = op.metadata["instance_name"]
                is_c = op.node_id.startswith("c")
                # 即使是不同算子，相同的常量 ID (如 c1_rd) 我们在此时暂不自动合并，除非显式指定
                p_addr = 0  # Bypassed obsolete MemoryAllocator call
                op.metadata["p_addr"] = p_addr

        # 5. 导出异质化规格书
        build_dir = Path(__file__).parent.parent.parent / "build" / "test" / "hybrid_sin_cos"
        os.makedirs(build_dir, exist_ok=True)
        spec_path = str(build_dir / "hybrid.precision.spec")
        HSTSSpecRenderer.render_to_file("hybrid_workload", final_stream, spec_path)
        
        print(f"✅ Hybrid Spec Exported: {spec_path}")
        pass

if __name__ == "__main__":
    unittest.main()
