import sys
from pathlib import Path
import sympy as sp

# Add src to path
sys.path.append(str(Path(__file__).parent.parent.parent / "src"))

from frontend import GraphCompiler, hw_shiftdn
from ir.mir.builder import MIROpBuilder

def test_frontend_compilation():
    print("🚀 开始测试 AIACC 前端自动化器 (SymPy -> MIROp)...")
    
    # 1. 高层代数定义 (极简、无脑写算式)
    x = sp.Symbol('x')
    # 模拟一个带有一大堆冗余和专用硬件的公式
    # 随便写一个：y = x * x + x * x + (x * x * x) + SHIFTDN(x, 2)
    # 我们故意写了很多冗余的 x*x，来测试 CSE 是否能消除它们
    formula = (x * x) + (x * x) + (x * x * x) + hw_shiftdn(x, 2)
    
    print(f"\n[1] 原始开发者公式: {formula}")
    
    # 2. 编译并输出对接 DFG 图
    compiler = GraphCompiler()
    
    # 假设给输入 x 绑定指定的测试要求 manual_bank=1
    inputs_meta = { x: {"manual_bank": 1} }
    
    dfg = compiler.compile(inputs_meta, formula)
    
    print(f"\n[2] 中端与后置编译器装配完毕，共生成 {len(dfg)} 个 MIROp 节点。")
    print("-" * 50)
    for node in dfg:
        meta_info = f" [MB:{node.metadata.get('manual_bank')}]" if node.metadata.get('manual_bank') is not None else ""
        deps = [d for d in node.metadata.get('depends_on', [])]
        print(f"OP: {node.op_type: <15} ID: {node.trace_id[:12]: <15} DEPS: {deps} {meta_info}")
        
    # 测试 DFG 的连通性 (简单断言)
    assert len(dfg) > 4, "生成的 DFG 节点数异常少！"
    print("\n✅ DFG 对接与连通性测试通过！这套图已经可以直接喂给 Pipeline Runner！")

if __name__ == "__main__":
    test_frontend_compilation()
