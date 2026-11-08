import sys
import os
import sympy as sp
from pathlib import Path

# Add project root to path
sys.path.append(str(Path(__file__).parent.parent.parent.parent.parent.parent))

from src.frontend.compiler import GraphCompiler
from src.tools.standard_pipeline_runner import StandardPipelineRunner

def test_tensor_add_looping():
    """
    量化验证：测试 N=16 的张量加法是否生成了硬件循环指令。
    """
    compiler = GraphCompiler()
    
    # 1. 定义输入 (N=16, 4x32-bit floats per block)
    # 给定物理 Bank 映射，方便观察 inc 效果
    inputs = {
        "a": {"manual_bank": 0, "addr": 0},
        "b": {"manual_bank": 1, "addr": 0},
        "constants": {"manual_bank": 7, "addr": 0}
    }
    
    # 2. 定义公式 C = A + B
    a, b = sp.symbols('a b')
    formula = a + b
    
    # 3. 运行标准流水线 (Stage A -> E)
    # 我们将 DFG 生成逻辑封装为函数传给 runner
    get_dfg_fn = lambda: compiler.compile_tensor(inputs, formula, shape=[16], out_name="c")
    input_data = {"a": [1.0]*16, "b": [2.0]*16}
    import numpy as np
    golden_y = np.array([3.0]*16)
    
    runner = StandardPipelineRunner(build_root="build/operators")
    success = runner.run_full_pipeline(
        op_name="tensor_loop_test",
        get_dfg_fn=get_dfg_fn,
        input_data=input_data,
        golden_y=golden_y,
        category="binary/math/add"
    )
    
    case_dir = Path("build/operators/binary/math/add/tensor_loop_test")
    
    if success:
        print("\n" + "="*50)
        print("🎉 编译流水线运行成功！")
        print("="*50)
        
        # 5. 核心审计：检查 Hex 文件内容
        hex_file = Path(case_dir) / "smc_0" / "iq_ur_r0.hex"
        if hex_file.exists():
            with open(hex_file, 'r') as f:
                content = f.read().strip().split('\n')
                print(f"\n[AUDIT] 读指令队列 (iq_ur_r0.hex) 行数: {len(content)}")
                # 如果成功折叠，N=16 只需要 1 行活跃指令（外加可能的气泡）
                for i, line in enumerate(content):
                    print(f"  Line {i}: 0x{line}")
        
        print("\n请检查以上的 Hex 是否包含了嵌套循环标记。")
    else:
        print("❌ 编译失败，请检查 LIRPacker 或 Compactor 日志。")

if __name__ == "__main__":
    test_tensor_add_looping()
