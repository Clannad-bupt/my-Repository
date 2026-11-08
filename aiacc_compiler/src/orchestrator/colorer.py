from typing import List, Dict, Tuple, Set

class BalancedBankColorer:
    """
    AIACC 8-Bank 均衡着色分配器。
    基于图着色启发式算法，解决 8-Bank SMC 物理资源冲突。
    """
    @staticmethod
    def assign_banks(tensors: List, conflicts: List[Tuple[str, str]], bank_num: int = 8):
        """
        分配 Bank ID。
        返回: (coloring, success)
        """
        adj = {}
        for t in tensors:
            adj[t.name] = []
            
        for u, v in conflicts:
            if u in adj and v in adj:
                adj[u].append(v)
                adj[v].append(u)
        
        coloring = {}
        # 1. 处理手动锁定
        for t in tensors:
            if hasattr(t, 'manual_bank') and t.manual_bank is not None:
                coloring[t.name] = t.manual_bank

        # 2. 启发式：按度数排序
        unassigned = [t for t in tensors if t.name not in coloring]
        unassigned.sort(key=lambda t: len(adj[t.name]), reverse=True)
        
        success = True
        for t in unassigned:
            used_colors = {coloring[neighbor] for neighbor in adj[t.name] if neighbor in coloring}
            
            # 计算合法 Bank 的负载情况
            bank_load = {b: 0 for b in range(bank_num)}
            for name, b in coloring.items():
                if b < bank_num: # 仅统计物理 Bank 的负载
                    bank_load[b] += 1
            
            # 寻找负载均衡下的最佳位置
            best_color = None
            min_load = float('inf')
            for b in range(bank_num):
                if b not in used_colors:
                    if bank_load[b] < min_load:
                        min_load = bank_load[b]
                        best_color = b
            
            if best_color is not None:
                coloring[t.name] = best_color
            else:
                success = False
                # 冲突无法消除，暂时溢出 (Overflow) 分配。
                # 溢出号计算：max(used_colors) + 1，确保之后计算 used_colors 时不会撞车
                ov_color = max(used_colors) + 1 if used_colors else bank_num
                coloring[t.name] = ov_color
                
        return coloring, success
