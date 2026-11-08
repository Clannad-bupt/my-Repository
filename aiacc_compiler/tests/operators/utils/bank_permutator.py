# tests/operators/utils/bank_permutator.py
from itertools import product

def get_unique_bank_layouts(num_actors):
    """
    🛸 [ADPC V4.0] 冲突等价类生成器
    动态生成 num_actors 个输入的所有物理冲突拓扑。
    
    算法核心：
    1. 穷举所有分配序列。
    2. 使用规范化形式 (Canonical Form) 过滤同构布局。
    3. 结果严格覆盖：全隔离、两两碰撞、全碰撞等所有物理边界。
    
    例如 num_actors=3:
    返回 5 种基本拓扑 (Bell Number B_3 = 5):
    - [0, 1, 2]: 无冲突
    - [0, 0, 1]: A-B 碰撞
    - [0, 1, 0]: A-C 碰撞
    - [1, 0, 0]: B-C 碰撞
    - [0, 0, 0]: 全碰撞
    """
    if num_actors <= 0:
        return [[]]
        
    layouts = []
    seen = set()
    # 在 num_actors 个虚拟 Bank 中穷举
    for combo in product(range(num_actors), repeat=num_actors):
        # 归一化：按照符号出现顺序重新赋予 Bank ID
        mapping = {}
        canonical = []
        next_id = 0
        for b in combo:
            if b not in mapping:
                mapping[b] = next_id
                next_id += 1
            canonical.append(mapping[b])
        
        canonical_tuple = tuple(canonical)
        if canonical_tuple not in seen:
            seen.add(canonical_tuple)
            layouts.append(list(canonical_tuple))
            
    return layouts

if __name__ == "__main__":
    # 单元测试
    for n in range(1, 4):
        res = get_unique_bank_layouts(n)
        print(f"Actors={n}, Unique Layouts={len(res)}: {res}")
