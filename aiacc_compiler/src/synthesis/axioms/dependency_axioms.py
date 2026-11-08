import z3
from typing import List, Dict, Any
from ir.mir.mir_op import MIROp
from api.hw_spec import HardwareSpec
from .base import AxiomBase

class DependencyAxioms(AxiomBase):
    """
    拓扑约束模块：处理 DFG 边权与时序顺序。
    """

    @staticmethod
    def add_latency_axiom(
        solver,
        ops: List[MIROp],
        t_vars: Dict[str, z3.ArithRef],
        II: int,
        spec: HardwareSpec,
        **kwargs
    ):
        """
        RAW 依赖公理（绝对时间）。
        确保消费者在生产者结果就绪后发射。
        """
        mode = kwargs.get("mode", "stable")
        for op in ops:
            for dep_id in op.metadata.get("depends_on", []):
                producer = next((o for o in ops if o.trace_id == dep_id), None)
                if not producer:
                    continue
                
                # 获取 Latency，V5 模式下严格检查
                op_type = producer.op_type.upper()
                if mode == "v5":
                    if op_type not in spec.delays:
                        raise KeyError(f"Critical SMT-V5 Error: Latency for '{op_type}' (Trace: {producer.trace_id}) missing in hardware spec!")
                    lat = spec.delays[op_type]
                else:
                    lat = spec.delays.get(op_type, 1)
                
                label = f"lat_{op.trace_id}_{dep_id}_{mode}"
                DependencyAxioms._add_constraint(
                    solver, t_vars[op.trace_id] >= t_vars[dep_id] + lat, label
                )

            # === RTOVR 增广边约束（Step 2）===
            latch_vars = kwargs.get("latch_vars")
            for prod_id, rtovr_info in op.metadata.get("rtovr_deps", {}).items():
                producer = next((o for o in ops if o.trace_id == prod_id), None)
                if not producer:
                    continue
                lat_p = spec.delays.get(producer.op_type.upper(), 1)
                # 🛸 [V6.0 LATCH OPTIMIZATION - HYBRID MODEL] 异步锁存与严格对齐混合建模
                # 创设虚拟锁存时间变量 t_latch
                t_latch = z3.Int(f"t_latch_{op.trace_id}_{prod_id}")
                if latch_vars is not None:
                    latch_vars[(op.trace_id, prod_id)] = t_latch
                
                # 1. 严格采样约束（生产者完成输出的时刻，RTOVR 必须在 1 拍内立即采样，防寄存器覆盖溢出）
                DependencyAxioms._add_constraint(
                    solver,
                    t_latch == t_vars[prod_id] + lat_p,
                    f"rtovr_strict_{op.trace_id}_{prod_id}"
                )
                
                # Multipliers, ALUs, UR_WRITE, and addition trees do not have elastic buffers, 
                # so they must be executed in the cycle immediately following latching (strict equality == t_latch + 1)
                # to match RtoVR's 1-cycle latency.
                latch_cond = (t_vars[op.trace_id] == t_latch + 1)

                DependencyAxioms._add_constraint(
                    solver,
                    latch_cond,
                    f"rtovr_latch_to_consumer_{op.trace_id}_{prod_id}"
                )
                
                # 3. 循环生命期安全约束（由于循环按 II 重复运行，本次迭代的消费者执行时刻必须在下一次 RTOVR 重写前完成）
                DependencyAxioms._add_constraint(
                    solver,
                    t_vars[op.trace_id] <= t_latch + II,
                    f"rtovr_latch_lifecycle_{op.trace_id}_{prod_id}"
                )

    @staticmethod
    def add_dependency_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        通用依赖公理包装器，符合标准签名。
        """
        DependencyAxioms.add_latency_axiom(solver, ops, t_vars, II, spec, **kwargs)
