import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))
from library.atomic.transcendentals.sin import get_sin_dfg_template

def debug_dfg():
    ops = get_sin_dfg_template()
    ids = [op.node_id for op in ops]
    print(f"Total Ops: {len(ops)}")
    print(f"Unique IDs: {len(set(ids))}")
    
    if len(ids) != len(set(ids)):
        print("❌ 发现重复的 Node ID!")
        import collections
        dups = [item for item, count in collections.Counter(ids).items() if count > 1]
        print(f"重复项: {dups}")
    
    # 检查依赖引用是否存在
    for op in ops:
        for dep in op.metadata.get("depends_on", []):
            if dep not in ids:
                print(f"❌ 悬挂引用: 节点 {op.node_id} 依赖于不存在的 {dep}")

if __name__ == "__main__":
    debug_dfg()
