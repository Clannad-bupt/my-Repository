from typing import List, Dict

class MemoryAllocator:
    """
    AIACC SMC 物理地址分配器。
    支持 8-Bank 物理隔离下的 128-bit(Word) 粒度分配与生命周期复用。
    """
    def __init__(self, hw_params):
        self.bank_depth = hw_params.PARAM_UR_BANK_DEPTH
        self.bank_num = hw_params.PARAM_UR_BANK_NUM

    def allocate(self, tensors: List) -> Dict[str, int]:
        """
        输入: 一个具备 bank_id, size, start_t, end_t 的张量列表
        输出: {tensor_name: base_addr}
        """
        results = {}
        assigned = [] # 已分配的张量列表
        
        # 逐个分配 (启发式：按 size 降序，更容易塞满碎片?)
        tensors_sorted = sorted(tensors, key=lambda t: t.size, reverse=True)
        
        for t in tensors_sorted:
            base_addr = 0
            # 找到当前 Bank 内所有可能冲突的张量
            conflicts = [a for a in assigned if a.bank_id == t.bank_id]
            
            # 寻找合适的空位
            while base_addr + t.size <= self.bank_depth:
                is_overlap = False
                for other in conflicts:
                    if self._is_physical_conflict(t, base_addr, other):
                        # 如果物理冲突，将起始位推移到冲突端的末尾
                        base_addr = other.base_addr + other.size
                        is_overlap = True
                        break
                
                if not is_overlap:
                    # 分配成功
                    t.base_addr = base_addr
                    results[t.name] = base_addr
                    assigned.append(t)
                    break
            else:
                raise MemoryError(f"Overflow: Bank {t.bank_id} depth {self.bank_depth} exceeded by {t.name}")
                
        return results

    @staticmethod
    def _is_physical_conflict(t, base_addr, other):
        """
        物理冲突判定：同时满足“地址重叠”与“时间重叠”。
        """
        # 1. 地址区间判定
        addr_overlap = not (base_addr + t.size <= other.base_addr or \
                             base_addr >= other.base_addr + other.size)
        if not addr_overlap:
            return False
            
        # 2. 生命周期判定 (如果时间不重叠，则不冲突，可复用)
        time_overlap = not (t.end_t < other.start_t or t.start_t > other.end_t)
        
        return time_overlap
