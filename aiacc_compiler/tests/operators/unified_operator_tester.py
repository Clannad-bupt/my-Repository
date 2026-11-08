# tests/operators/unified_operator_tester.py
import numpy as np
import sympy as sp
import sys
import os
from pathlib import Path
from typing import Dict, Any, Callable, List

# 环境路径注入
ws = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ws / "src"))

from tools.standard_pipeline_runner import StandardPipelineRunner
from frontend import GraphCompiler
from tools.pipeline_utils import apply_bank_overrides
from utils.bank_permutator import get_unique_bank_layouts

class UnifiedOperatorTester:
    """
    🛸 [ADPC V4.0] 算子标准化生产工厂
    高度抽象的算子验证框架，支持：
    1. 前端 DFG 自动构造
    2. 等价类 Bank Fuzzing 覆盖
    3. 比特级仿真比对
    """
    def __init__(self, hw_config="hw_config", build_root="build/operators"):
        self.runner = StandardPipelineRunner(hw_config=hw_config, build_root=build_root)
        self.compiler = GraphCompiler()

    def run_operator_test(self, 
                          op_name: str, 
                          category: str, 
                          formula_fn: Callable[[List[sp.Symbol]], Any], 
                          golden_fn: Callable[[Dict[str, np.ndarray]], np.ndarray], 
                          symbols: List[sp.Symbol],
                          meta_role_map: Dict[sp.Symbol, str] = None,
                          input_range: tuple = (0.5, 1.5),
                          run_fuzz: bool = True):
        
        print(f"\n🏭 [FACTORY] Launching Production Line for: {op_name} ({category})")
        
        # 1. 生成物理拓扑等价类
        # 我们考虑 n 个变量输入 + 1 个常量池输入
        num_actors = len(symbols) + 1 
        layouts = get_unique_bank_layouts(num_actors) if run_fuzz else [list(range(num_actors))]
        print(f"   - Coverage targets: {len(layouts)} unique bank topologies.")

        total_pass = 0
        for i, layout in enumerate(layouts):
            print(f"   [Trial {i+1}/{len(layouts)}] Bank Layout: {layout}")
            
            # 建立 Symbol名称 -> BankID 的映射
            bank_map = {symbols[j].name: layout[j] for j in range(len(symbols))}
            bank_map["constants"] = layout[-1] # 最后一个 actor 是常量池
            
            # 👽 每次 Trial 强制重新实例化编译环境，物理隔离
            self.compiler = GraphCompiler()
            
            # 2. 准备 DFG 生产函数
            def get_dfg_fn():
                # 构造输入元数据
                inputs_meta = {}
                for s in symbols:
                    role = meta_role_map.get(s, "operand_a") if meta_role_map else "operand_a"
                    inputs_meta[s] = {"role": role}
                
                # A阶段：生成 MIR 节点
                dfg = self.compiler.compile(inputs_meta, formula_fn(symbols), out_name="y")
                
                # 注入动态 Bank 覆盖 (Fuzzing 核心)
                return apply_bank_overrides(dfg, bank_map)

            # 3. 准备数据
            fx_data = {s.name: np.random.uniform(input_range[0], input_range[1], size=(4,)).astype(np.float32) for s in symbols}
            golden_y = golden_fn(fx_data)

            # 4. 执行流水线
            try:
                success = self.runner.run_full_pipeline(
                    op_name=op_name,
                    get_dfg_fn=get_dfg_fn,
                    input_data=fx_data,
                    golden_y=golden_y,
                    category=category,
                    layout_id=i
                )
                if success: total_pass += 1
            except Exception as e:
                print(f"   ❌ Layout Exception: {str(e)}")

        # 5. 统计报告
        print(f"\n🏁 Finished {op_name}: Passed {total_pass}/{len(layouts)} layouts.")
        return total_pass == len(layouts)

if __name__ == "__main__":
    # 示例运行 (需确保 utils/bank_permutator.py 存在)
    tester = UnifiedOperatorTester()
    x = sp.Symbol('x')
    tester.run_operator_test(
        op_name="reciprocal_factory_test",
        category="demo",
        formula_fn=lambda syms: 1.0 / syms[0],
        golden_fn=lambda data: 1.0 / data['x'],
        symbols=[x],
        run_fuzz=True
    )
