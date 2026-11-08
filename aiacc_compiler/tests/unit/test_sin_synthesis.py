import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg

def test_sin_8th_order_synthesis():
    print("🚀 开始合成物理级联版 sin APB 时序 (UR->RtoVR->Unit->...->UR_W)...")
    
    spec = HardwareSpec.load("hw_config")
    synthesizer = SMTSynthesizer(spec)
    
    # 1. 加载模板
    ops = get_sin_8order_dfg()
    print(f"   [Input] 模板共有 {len(ops)} 个脉冲节点")
    
    # 2. 求解
    result = synthesizer.solve(ops)
    
    if result:
        print("   [SAT] 成功找到物理可行解 (T_start 序列):")
        # 按时间排序输出
        sorted_res = sorted(result.items(), key=lambda x: x[1])
        for node_id, t in sorted_res:
             op = next(o for o in ops if o.node_id == node_id)
             print(f"      T={t:02d}: {op.op_type} ({op.node_id[:4]})")
        
        # 验证最后的 T 尽可能小
        max_t = max(result.values())
        print(f"\n✅ 合成成功! 总延迟 (算子发射端): {max_t} 拍 (未计入最后一拍 Pipeline Delay)")
    else:
        print("❌ 核心算子合成失效 (UNSAT)")

if __name__ == "__main__":
    test_sin_8th_order_synthesis()
