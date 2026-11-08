import unittest
import sympy as sp
from frontend.compiler import GraphCompiler
from frontend.optimizer import PhysicalGraphOptimizer
from api.hw_spec import HardwareSpec

class TestDFGSpillPruning(unittest.TestCase):
    def test_pure_scalar_graph_avoids_spill(self):
        """
        [Level 2] DFG 维度审查：纯标量计算图豁免 Spill。
        模拟常数多项式展开，比如 (c1 + c2 * c3) / c4
        """
        spec = HardwareSpec.load("hw_config")
        
        c1, c2, c3, c4 = sp.symbols("c1 c2 c3 c4")
        # 构造一个会产生高扇出的标量表达式
        formula = (c1 + c2 * c3) * c4 + (c1 + c2 * c3) * c2 + c1
        
        # 所有的输入都被定义为标量 (stride = 0)
        inputs_meta = {
            "c1": {"logical_addr": 100, "stride": 0, "role": "param"},
            "c2": {"logical_addr": 101, "stride": 0, "role": "param"},
            "c3": {"logical_addr": 102, "stride": 0, "role": "param"},
            "c4": {"logical_addr": 103, "stride": 0, "role": "param"},
        }
        
        # 检查所有的输入是否为标量
        all_scalars = all(meta.get("stride", 1) == 0 for meta in inputs_meta.values())
        
        optimizer = PhysicalGraphOptimizer(scratch_bank=6, bank_depth=256)
        if all_scalars:
            optimizer.disable_spill = True  # 测试 TDD 新增的开关
            
        compiler = GraphCompiler(optimizer=optimizer)
        
        # 编译 DFG
        ops = compiler.compile(inputs_meta, formula, out_name="y", spec=spec)
        
        # 断言 1：图规模必须非常小 (不应该有大量的 spill-reload 节点爆炸)
        # 如果溢出保护没有被禁用，高扇出的 (c1 + c2 * c3) 会引发大量 spill-reload。
        self.assertTrue(len(ops) <= 30, f"DFG scale exploded to {len(ops)} nodes, expecting <= 30.")
        
        # 断言 2：不存在任何 role="intermediate" 的内存读写操作
        intermediate_mem_ops = [
            op for op in ops 
            if op.op_type in ["UR_READ", "UR_WRITE"] and op.metadata.get("role") == "intermediate"
        ]
        self.assertEqual(len(intermediate_mem_ops), 0, "Found intermediate memory spill/reload ops in a pure scalar graph!")

if __name__ == "__main__":
    unittest.main()
