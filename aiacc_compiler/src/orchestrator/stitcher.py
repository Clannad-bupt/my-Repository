from typing import List, Dict, Set, Optional

class HSTSStitcher:
    """
    AIACC HSTS 异构算子缝合器 (v2: 依赖感知识别版)。
    负责将算子拓扑展开为全球周期精确 (Global-Cycle-Accurate) 的指令流。
    """
    def __init__(self):
        self.final_stream = []
        self.occupied_t_iq: Dict[int, Set[int]] = {}   # {global_t: set(iq_ids)}
        self.occupied_t_bank: Dict[int, Set[int]] = {} # {global_t: set(bank_ids)}
        
        # 记录算子实例的生命周期: {instance_name: (start_t, end_t)}
        self.op_metadata: Dict[str, Dict] = {}
        self.instance_count = {}

    def _check_conflict(self, stream: List, synthesis_res: Dict, offset: int) -> bool:
        """物理层突发冲突审计 (IQ/Bank)"""
        schedule = synthesis_res.get("schedule", synthesis_res)
        for op in stream:
            local_t = schedule[op.trace_id]
            global_t = local_t + offset
            
            # IQ 并发审计
            if global_t in self.occupied_t_iq:
                if "iq_id" in op.metadata and op.metadata["iq_id"] in self.occupied_t_iq[global_t]:
                    print(f"[CONFLICT] IQ {op.metadata['iq_id']} at Global T:{global_t} (local_t={local_t}, offset={offset})")
                    return True
            
            # Bank 端口并发审计
            if global_t in self.occupied_t_bank:
                if "bank_id" in op.metadata and op.metadata["bank_id"] in self.occupied_t_bank[global_t]:
                    print(f"[CONFLICT] Bank {op.metadata['bank_id']} at Global T:{global_t} (local_t={local_t}, offset={offset})")
                    return True
        return False

    def add_operator(self, name: str, stream: List, synthesis_res: Dict[str, int], 
                     depends_on: Optional[List[str]] = None,
                     force_min_offset: int = 0) -> int:
        """
        缝合一个新算子。
        - name: 算子类型
        - stream: 本地算子流
        - depends_on: 依赖的前驱算子实例名列表 (e.g., ["ADD_0"])
        - force_min_offset: 强制约束的最小起始偏移量
        """
        # 1. 识别实例 ID
        self.instance_count[name] = self.instance_count.get(name, 0) + 1
        instance_name = f"{name}_{self.instance_count[name] - 1}"
        
        # 2. 计算最小起始偏移 (Min Offset)
        min_offset = force_min_offset
        if depends_on:
            for dep in depends_on:
                if dep in self.op_metadata:
                    dep_end = self.op_metadata[dep]["end_t"]
                    min_offset = max(min_offset, dep_end + 1)
        
        # 3. 寻找无物理冲突的起始点
        offset = min_offset
        while self._check_conflict(stream, synthesis_res, offset):
            offset += 1
            
        schedule = synthesis_res.get("schedule", synthesis_res)
        max_local_t = max(schedule.values()) if schedule else 0
        busy_cycles = 64 # 256 位宽的物理实消耗拍数
        
        self.op_metadata[instance_name] = {
            "start_t": offset,
            "end_t": offset + max_local_t + busy_cycles 
        }
        
        for op in stream:
            local_t = schedule[op.trace_id]
            global_t = local_t + offset
            
            # 属性刷新
            op.metadata["global_t"] = global_t
            op.metadata["t"] = local_t
            op.metadata["instance_name"] = instance_name
            
            # 全局物理足迹占用
            if "iq_id" in op.metadata:
                self.occupied_t_iq.setdefault(global_t, set()).add(op.metadata["iq_id"])
            if "bank_id" in op.metadata:
                self.occupied_t_bank.setdefault(global_t, set()).add(op.metadata["bank_id"])
            
            self.final_stream.append(op)
            
        print(f"HSTS: [{instance_name}] stitched at Global T:{offset} (ends at {offset + max_local_t}).")
        return offset

    def get_final_stream(self) -> List:
        return sorted(self.final_stream, key=lambda x: x.metadata["global_t"])
