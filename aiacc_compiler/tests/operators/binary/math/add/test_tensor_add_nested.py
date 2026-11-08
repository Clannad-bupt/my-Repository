import sys
import os
import sympy as sp
from pathlib import Path
import numpy as np

# Add project root and src to path
root = Path(__file__).parent.parent.parent.parent.parent.parent
sys.path.append(str(root))
sys.path.append(str(root / "src"))

from src.frontend.compiler import GraphCompiler
from src.tools.standard_pipeline_runner import StandardPipelineRunner

def test_tensor_add_nested():
    """
    量化验证：测试 Shape=[4, 16] 的嵌套循环。
    外层 4 次，内层 16 元素 (4 blocks)。
    """
    compiler = GraphCompiler()
    
    inputs = {
        "a": {"manual_bank": 0, "addr": 0},
        "b": {"manual_bank": 1, "addr": 0},
        "constants": {"manual_bank": 7, "addr": 0}
    }
    
    a, b = sp.symbols('a b')
    formula = a + b
    
    # 3. 运行标准流水线
    # shape=[4, 16] -> 64 elements
    get_dfg_fn = lambda: compiler.compile_tensor(inputs, formula, shape=[4, 16], out_name="c")
    input_data = {"a": [1.0]*64, "b": [2.0]*64}
    golden_y = np.array([3.0]*64)
    
    runner = StandardPipelineRunner(build_root="build/operators")
    success = runner.run_full_pipeline(
        op_name="tensor_nested_test",
        get_dfg_fn=get_dfg_fn,
        input_data=input_data,
        golden_y=golden_y,
        category="binary/math/add"
    )
    
    case_dir = Path("build/operators/binary/math/add/tensor_nested_test")
    
    if success:
        print("\n" + "="*50)
        print("🎉 嵌套循环编译成功！")
        print("="*50)
        
        # 审计：检查生成的 Hex。
        # 理论上应该有 4 个指令：
        # 1. UR_READ (Outer Header, Level 1, Loops=3, Inc_Embed=4)
        # 2. UR_READ (Body, Level 0, Loops=3, Inc=1)
        # 3. FPADD   (Body, Level 0, Loops=3)
        # 4. UR_WRITE (Body, Level 0, Loops=3, Embed_End=1)
        
        hex_file = Path(case_dir) / "smc_0" / "iq_ur_r0.hex"
        if hex_file.exists():
            with open(hex_file, 'r') as f:
                content = [line.strip() for line in f.readlines() if line.strip()]
                print(f"\n[AUDIT] 读指令队列行数: {len(content)}")
                for i, line in enumerate(content):
                    # 手动解析 Header (假设 bits_code=16)
                    val = int(line, 16)
                    header = val >> 16
                    # Level (EMBED_LOOPS) is at [36-38] of Header? 
                    # No, logic is complex. Just print it.
                    print(f"  Line {i}: {line} (Header: {header:08x})")
    else:
        print("❌ 编译失败。")

if __name__ == "__main__":
    test_tensor_add_nested()
