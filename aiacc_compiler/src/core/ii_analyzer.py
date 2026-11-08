"""
AIACC II 分析器 (V1.0)
========================
职责：对给定 DFG 静态分析理论最优 Initiation Interval。

公式：
  II_res = max over resource types of: ceil(usage_count / hw_instances) * latency_align
  II_rec = 0  (sin/cos 等纯前向 DFG 无循环携带依赖)
  II     = max(II_res, II_rec)

不依赖 Z3，纯静态分析，速度极快。
SMT 仍负责验证具体调度可行性；II 值由本模块提供给 LoopStitcher。
"""
import math
from typing import List, Dict
from ir.mir.mir_op import MIROp


# 硬件资源实例数（从 system_core.yaml 同步，此处为常量镜像）
_DEFAULT_INSTANCES = {
    "FPMUL":  1,
    "FPADD":  1,
    "INTADD": 1,
    "LOGIC":  1,
    "UR_READ":  4,
    "UR_WRITE": 4,
}

# 单元延迟（与 pipeline_delays.yaml 保持一致）
_DEFAULT_LATENCY = {
    "FPMUL":  4,
    "FPADD":  4,
    "INTADD": 1,
    "LOGIC":  1,
    "UR_READ":  1,
    "UR_WRITE": 1,
}


class IIAnalyzer:
    """
    AIACC Initiation Interval 静态分析器。

    使用方式：
        analyzer = IIAnalyzer(hw_spec)
        II = analyzer.compute(dfg_ops)
    """

    def __init__(self, hw_spec=None):
        """
        Args:
            hw_spec: HardwareSpec 对象（可选）。
                     若提供，则从硬件规格读取实例数和延迟；
                     否则回退到模块级默认值。
        """
        self.instances: Dict[str, int] = dict(_DEFAULT_INSTANCES)
        self.latency:   Dict[str, int] = dict(_DEFAULT_LATENCY)

        if hw_spec is not None:
            self._load_from_spec(hw_spec)

    def _load_from_spec(self, hw_spec):
        """从 HardwareSpec 覆盖默认值。"""
        try:
            unit_instances = (
                hw_spec.config
                .get("resource_constraints", {})
                .get("unit_instances", {})
            )
            for k, v in unit_instances.items():
                self.instances[k.upper()] = v
        except Exception:
            pass  # 规格缺失时静默回退

        try:
            for k, v in hw_spec.delays.items():
                self.latency[k.upper()] = v
        except Exception:
            pass

    def compute(self, ops: List[MIROp]) -> int:
        """
        计算 DFG 的理论 II（Initiation Interval）。

        算法：
          1. 统计每种资源类型的使用次数
          2. II_res = max(ceil(usage / instances) for each resource)
             其中取延迟整除对齐：ceil_to_latency(II_res, lat)
          3. II = max(1, II_res)   # 至少为 1

        Returns:
            int: 理论最优 II（cycles）
        """
        usage: Dict[str, int] = {}
        for op in ops:
            key = op.op_type.upper()
            # RTOVR 是 Crossbar，不作为资源瓶颈统计
            if key == "RTOVR":
                continue
            usage[key] = usage.get(key, 0) + 1

        II_res = 1
        bank_usage: Dict[int, int] = {}
        for op in ops:
            bid = op.metadata.get("manual_bank")
            if bid is not None:
                bank_usage[bid] = bank_usage.get(bid, 0) + 1

        breakdown = {}
        for res, count in usage.items():
            inst = self.instances.get(res, 1)
            lat  = self.latency.get(res, 1)

            # 资源约束 II（标准模调度公式）：
            #   II_res = ceil(usage_count / hw_instances)
            ii_for_res = math.ceil(count / inst)

            breakdown[res] = {
                "count": count, "instances": inst,
                "latency": lat, "II_res": ii_for_res,
            }
            II_res = max(II_res, ii_for_res)

        # Bank 冲突下界：每个 Bank 每拍只能访问一次
        II_bank = max(bank_usage.values()) if bank_usage else 1
        II_res = max(II_res, II_bank)

        # 打印分析报告
        print(f"[II-ANALYSIS] Resource breakdown:")
        for res, d in breakdown.items():
            print(f"  {res:15s}: count={d['count']}, inst={d['instances']}, "
                  f"lat={d['latency']} → II_res={d['II_res']}")
        if bank_usage:
            max_b = max(bank_usage, key=bank_usage.get)
            print(f"  BANK-CONFLICT  : max_access={bank_usage[max_b]} (Bank {max_b}) → II_bank={II_bank}")
        print(f"[II-ANALYSIS] Theoretical MII = {II_res} cycles")

        return II_res
