from typing import Dict, ClassVar
from collections import Counter
from pydantic import BaseModel, Field

class FootprintMask(BaseModel):
    """
    AIACC HSTS 物理足迹掩码：记录算子在每一拍占用的具体硬件资源及其数量。
    支持多端口资源冲突检测。
    """
    # 时间轴 -> 资源计数 (Counter[ResourceID -> Count])
    timeline: Dict[int, Counter] = Field(default_factory=dict)
    
    # 硬件能力限制 (ResourceID -> Capacity)
    # 定义为类常量 (ClassVar)
    CAPACITIES: ClassVar[Dict[str, int]] = {
        "bank": 1, 
        "iq": 1,
        "slot": 1
    }

    def add_resource(self, t: int, resource_id: str):
        if t not in self.timeline:
            self.timeline[t] = Counter()
        self.timeline[t][resource_id] += 1

    def conflicts_with(self, other: 'FootprintMask', offset: int = 0) -> bool:
        """
        基于计数和容量限制进行冲突检测。
        """
        for t_other, res_other in other.timeline.items():
            t_self = t_other + offset
            if t_self in self.timeline:
                # 检查合并后的资源总数是否超过物理上限
                for rid, count in res_other.items():
                    current_count = self.timeline[t_self].get(rid, 0)
                    total_count = current_count + count
                    
                    # 识别资源分类并检查容量 (rid 可能是 "bank_1_rd", "iq_100" 等)
                    parts = rid.split("_")
                    base_type = parts[0] # bank, iq, slot
                    
                    # 查找对应类型的容量限制
                    capacity = self.CAPACITIES.get(base_type, 1)
                    
                    if total_count > capacity:
                        # 发现物理层冲突 (例如一拍多读同一 Bank)
                        return True
        return False

    def merge(self, other: 'FootprintMask', offset: int = 0):
        for t_other, res_other in other.timeline.items():
            t_self = t_other + offset
            if t_self not in self.timeline:
                self.timeline[t_self] = Counter()
            self.timeline[t_self].update(res_other)
