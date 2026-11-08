import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from library.atomic.transcendentals.sin import get_sin_8order_dfg

def print_data_path_alignment_table(ops, result):
    """
    生成直观的数据通路对齐表，用于人工审核公式与时序的对应关系。
    """
    if not result: return
    
    print("\n" + "="*110)
    print(f"{'数学公式步骤 (Math)':<25} | {'操作数 (Input)':<15} | {'搬运 (Route)':<12} | {'启动 (Start)':<10} | {'结果有效 (Valid)':<10} | {'去向 (Next)':<12}")
    print("-" * 110)
    
    # 按照计算逻辑分组 (FPMUL/FPADD 为中心)
    comp_ops = [op for op in ops if op.op_type in ["FPMUL", "FPADD"]]
    
    for comp in comp_ops:
        math_desc = comp.metadata.get("math", "N/A")
        t_start = result[comp.node_id]
        t_valid = t_start + 4 # 算子延迟 4
        
        # 查找它的输入路由
        route_ins = [o for o in ops if comp.node_id in o.metadata.get("depends_on", [])]
        route_iqs = "/".join([str(o.metadata.get('iq_id')) for o in route_ins])
        t_route = "/".join([str(result[o.node_id]) for o in route_ins])
        
        # 查找数据的原始来源 (UR_READ)
        src_names = []
        for r in route_ins:
            for dep in r.metadata.get("depends_on", []):
                src = next((o for o in ops if o.node_id == dep), None)
                if src: src_names.append(src.node_id)
        
        # 查找它的输出去向 (哪个 Rtovr 搬运了它的结果)
        route_outs = [o for o in ops if comp.node_id in o.metadata.get("depends_on", [])]
        # 注意：这里会包含输入路由，需要过滤出时间在 comp 之后的
        real_out = [o for o in route_outs if result[o.node_id] >= t_valid]
        t_next = "/".join([str(result[o.node_id]) for o in real_out])
        target_iq = "/".join([str(o.metadata.get('iq_id')) for o in real_out])

        print(f"{math_desc:<25} | {str(src_names):<15} | {t_route:<12} | {t_start:<10} | {t_valid:<10} | {t_next} (IQ:{target_iq})")

def test_sin_detailed_alignment():
    spec = HardwareSpec.load("hw_config")
    synthesizer = SMTSynthesizer(spec)
    ops = get_sin_8order_dfg()
    result = synthesizer.solve(ops)
    
    if result:
        print_data_path_alignment_table(ops, result)
        print("="*110)
    else:
        print("❌ UNSAT")

if __name__ == "__main__":
    test_sin_detailed_alignment()
