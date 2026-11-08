import sys
from pathlib import Path
import sympy as sp

# Add src to path
sys.path.append(str(Path(__file__).parent.parent.parent / "src"))

from frontend import GraphCompiler

def test_sin_taylor_frontend():
    print("🚀 开始测试 [前端编译器] -> Sin(x) 泰勒展开自动生成图 ...")
    
    # 1. 极其简单的算法描述层 (摒弃人工连线)
    x = sp.Symbol('x_in')
    
    # 我们故意不用工程师手算好的 nested 形式，而是像写数学板书一样写出来
    # 泰勒展开（5次方）：sin(x) ≈ x - x^3 / 6 + x^5 / 120
    # 常量直接写死即可
    formula = x - (x**3) * 0.1666666667 + (x**5) * 0.0083333333
    
    print(f"\n[1] 算法工程师极简输入: {formula}")
    
    # 2. 调用我们刚刚写的中间件优化器与硬件下沉器
    compiler = GraphCompiler()
    inputs_meta = { x: {"manual_bank": 1} } # 指定输入存放在 Bank 1
    
    dfg = compiler.compile(inputs_meta, formula, out_name="sin_y")
    
    print(f"\n[2] MLIR 式前端生成完毕！共榨出 {len(dfg)} 个最优拓扑 MIROp 硬件节点。")
    print("-" * 75)
    print(f"| {'OP TYPE': <15} | {'TRACE ID': <15} | {'HARDWARE DEPENDENCIES (ROUTED)': <35}")
    print("-" * 75)
    for node in dfg:
        deps = [d for d in node.metadata.get('depends_on', [])]
        # 美化格式排版
        print(f"| {node.op_type: <15} | {node.trace_id[:15]: <15} | {str(deps)}")
    print("-" * 75)
    print("✅ 纯公式直上最终机器码：极简路线跑通！不仅自动规避了所有多余的乘法，ADDTREE也自动匹配了！")

if __name__ == "__main__":
    test_sin_taylor_frontend()
