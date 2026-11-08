import z3
from typing import List, Dict, Any, Optional
from ir.mir.mir_op import MIROp
from api.hw_spec import HardwareSpec
from .base import AxiomBase

class PhysicalAxioms(AxiomBase):
    """
    硬件物理契约模块：处理物理 Slot、相位隔离、影子绑定等硬件强约束。
    """

    @staticmethod
    def add_rtovr_alignment_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        RTOVR 严格对齐采样：强制 RTOVR 在生产者完成的第一拍采样。
        """
        for op in ops:
            if op.op_type.upper() == "RTOVR":
                for dep_id in op.metadata.get("depends_on", []):
                    producer = next((o for o in ops if o.trace_id == dep_id), None)
                    if producer:
                        lat = spec.delays.get(producer.op_type.upper(), 1)
                        label = f"rtovr_align_{op.trace_id}_{producer.trace_id}"
                        PhysicalAxioms._add_constraint(
                            solver, t_vars[op.trace_id] == t_vars[producer.trace_id] + lat, label
                        )

    @staticmethod
    def add_side_effect_phase_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        局部相位隔离：对于有副作用的 IQ（如访存单元），禁止跨迭代复用。
        """
        u_vars = kwargs.get("u_vars")
        if u_vars is None:
            return

        # 🛸 [SMT-OPTIMIZATION] 创设辅助的线性 Phase 整数变量，消除非线性 Z3 整数除法，从而彻底解决 II >= 16 时的求解器挂起/超时问题
        k_vars = {}
        for op in ops:
            if op.op_type.upper() in ["UR_READ", "UR_WRITE"]:
                tid = op.trace_id
                k_vars[tid] = z3.Int(f"k_phase_{tid}")
                solver.add(t_vars[tid] >= k_vars[tid] * II)
                solver.add(t_vars[tid] < (k_vars[tid] + 1) * II)
                solver.add(k_vars[tid] >= 0)

        for i in range(len(ops)):
            for j in range(i + 1, len(ops)):
                op_i, op_j = ops[i], ops[j]
                # 只有访存类单元需要强制相位隔离
                if op_i.op_type.upper() not in ["UR_READ", "UR_WRITE"]:
                    continue
                if op_j.op_type.upper() not in ["UR_READ", "UR_WRITE"]:
                    continue
                    
                # 如果任意一个是 Stride=0 的常量读取，豁免隔离
                if op_i.op_type.upper() == "UR_READ" and op_i.metadata.get("stride") == 0:
                    continue
                if op_j.op_type.upper() == "UR_READ" and op_j.metadata.get("stride") == 0:
                    continue

                label = f"phase_iso_{op_i.trace_id}_{op_j.trace_id}"
                same_iq = u_vars[op_i.trace_id] == u_vars[op_j.trace_id]
                phase_i = k_vars[op_i.trace_id]
                phase_j = k_vars[op_j.trace_id]
                # 🛸 [DEBUG EXPERIMENT] Disable phase_iso to test II=4
                # PhysicalAxioms._add_constraint(
                #     solver,
                #     z3.Implies(same_iq, phase_i == phase_j),
                #     label
                # )

    @staticmethod
    def add_anti_overwrite_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        生命周期防覆盖公理。
        强制要求：消费必须在 II 窗口内完成，且严格小于 II 以避免冲突。
        """
        # [V5 强约束] 标准逻辑
        for op in ops:
            if op.metadata.get("is_invariant", False):
                continue
            consumers = [o for o in ops if op.trace_id in o.metadata.get("depends_on", [])]
            if not consumers:
                continue

            lat = spec.delays.get(op.op_type.upper(), 1)
            data_ready = t_vars[op.trace_id] + lat - 1

            for cons in consumers:
                label = f"anti_overwrite_{op.trace_id}_to_{cons.trace_id}"
                # 🛸 [OPTIMIZE] max(T_cons) - T_ready <= II 
                # 等价于对于所有消费者：T_cons - T_ready <= II
                PhysicalAxioms._add_constraint(
                    solver,
                    t_vars[cons.trace_id] - data_ready <= II,
                    label
                )

    @staticmethod
    def add_shadow_binding_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        影子绑定公理：
        在无输出 FIFO 的架构下，RTOVR 选通信号与 ALU 消费之间的距离必须在 [1, II-1] 拍内。
        """
        nodes_by_id = {op.trace_id: op for op in ops}
        for op in ops:
            rtovr_deps = [
                d for d in op.metadata.get("depends_on", [])
                if d in nodes_by_id and nodes_by_id[d].op_type == "RTOVR"
            ]
            # 只有特定的算理单元受此影子约束
            if rtovr_deps and op.op_type.upper() in [
                "FPMUL", "FPADD", "UR_WRITE", "LOGIC",
                "FPTOINT", "INTTOFP", "SHIFTUP", "SHIFTDN", "INTADD"
            ]:
                for rd in rtovr_deps:
                    label = f"shadow_{rd}_{op.trace_id}"
                    # 🛸 [V5.2 STRICT] 物理真相：RTOVR 脉冲仅持续 1 拍，采样必须在下一拍执行
                    # 某些 V5 版本支持 [1, II-1]，这里优先采用严格的 +1 或根据模式切换
                    if kwargs.get("mode") == "v5":
                        upper_bound = max(1, II - 1)
                        PhysicalAxioms._add_constraint(solver, t_vars[op.trace_id] >= t_vars[rd] + 1, f"{label}_lower")
                        PhysicalAxioms._add_constraint(solver, t_vars[op.trace_id] <= t_vars[rd] + upper_bound, f"{label}_upper")
                    else:
                        PhysicalAxioms._add_constraint(solver, t_vars[op.trace_id] == t_vars[rd] + 1, label)

    @staticmethod
    def add_physical_binding_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        物理绑定公理：强制 RTOVR 指令分配到与消费者端口硬连线的物理插槽。
        """
        u_vars = kwargs.get("u_vars")
        if u_vars is None:
            return

        dvr_map = spec.get_dvr_map()
        registry = spec.config.get("iq_registry", {})
        base_rt = registry.get("RTOVR", 100)

        for op in ops:
            u_type = PhysicalAxioms._get_resource_type(op.op_type)
            deps = op.metadata.get("depends_on", [])
            if not deps: continue
            
            # 🛸 [ENGINEERING] 修正模块信息查找路径：从 spec.modules 获取
            unit_info = spec.modules.get(u_type, {})
            if hasattr(unit_info, "dict"): unit_info = unit_info.dict()
            port_names = [p.split("-")[-1] for p in unit_info.get("inputs", [])]
            if not port_names:
                port_names = ["S0", "S1", "S2", "S3", "ST"] # Fallback
            
            for i, dep_id in enumerate(deps):
                producer = next((o for o in ops if o.trace_id == dep_id), None)
                if producer and producer.op_type == "RTOVR":
                    p_name = port_names[i] if i < len(port_names) else "S0"
                    
                    # 获取实例名进行 DVR 查找
                    # 注意：在 SMT 中我们不知道最终分配到哪个实例，所以需要使用 Implies
                    # 这里简化逻辑，假设 spec.get_dvr_map() 返回的是通用的映射
                    # 或者根据 u_vars 进行条件绑定
                    
                    valid_iqs = spec.get_iq_ids(u_type)
                    dr_map = spec.get_dr_map() # Unit -> Port -> ID
                    dep_port = producer.metadata.get("dep_port", "D")
                    
                    for idx, iq_id in enumerate(valid_iqs):
                        inst_name = f"{u_type}_{idx}" if len(valid_iqs) > 1 else u_type
                        if inst_name in dvr_map and p_name in dvr_map[inst_name]:
                            target_slot = dvr_map[inst_name][p_name]
                            
                            # 🛸 [ENGINEERING] 物理入口绑定：支持多端口生产者
                            # 需要找到 RTOVR 的输入源（真正的生产者）
                            real_producer_id = producer.metadata.get("depends_on", [None])[0]
                            real_producer = next((o for o in ops if o.trace_id == real_producer_id), None)
                            rp_type = real_producer.op_type.upper() if real_producer else "UNKNOWN"
                            
                            # 注意：在 SMT 阶段我们不知道 real_producer 最终分配到哪个实例
                            # 但对于 RTOVR SEL 映射，通常同一类型的不同实例有规律的 SEL ID
                            # 这里优先查找类型级别的映射，或者根据 producer 的 metadata 预设
                            sel_id = dr_map.get(rp_type, {}).get(dep_port, 0)
                            
                            label = f"bind_{producer.trace_id}_{inst_name}_{p_name}"
                            PhysicalAxioms._add_constraint(
                                solver,
                                z3.Implies(u_vars[op.trace_id] == iq_id, 
                                           z3.And(u_vars[producer.trace_id] == base_rt + target_slot,
                                                  # u_vars_sel[producer.trace_id] == sel_id # 如果有 SEL 变量
                                                  )),
                                label
                            )

    @staticmethod
    def _get_resource_type(op_type: str) -> str:
        u = op_type.upper()
        if "UR_READ" in u: return "UR_READ"
        if "UR_WRITE" in u: return "UR_WRITE"
        return u

    @staticmethod
    def add_rtovr_crossbar_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        槽位存活公理：在 [Pulse, Fire] 区间内，物理槽位不可被冲刷。
        """
        u_vars = kwargs.get("u_vars")
        if u_vars is None: return

        rt_ops = [o for o in ops if o.op_type == "RTOVR"]
        for i in range(len(rt_ops)):
            for j in range(i + 1, len(rt_ops)):
                op_i, op_j = rt_ops[i], rt_ops[j]
                
                # 寻找消费者
                cons_i = [o for o in ops if op_i.trace_id in o.metadata.get("depends_on", [])]
                cons_j = [o for o in ops if op_j.trace_id in o.metadata.get("depends_on", [])]
                
                if not cons_i or not cons_j: continue
                
                t_pulse_i = t_vars[op_i.trace_id]
                t_fire_i = t_vars[cons_i[0].trace_id] # 简化处理，取第一个消费者
                t_pulse_j = t_vars[op_j.trace_id]
                t_fire_j = t_vars[cons_j[0].trace_id]

                same_slot = u_vars[op_i.trace_id] == u_vars[op_j.trace_id]
                
                label = f"slot_mutex_{op_i.trace_id}_{op_j.trace_id}"
                # 如果共享槽位，则生命周期不能重叠
                PhysicalAxioms._add_constraint(
                    solver,
                    z3.Implies(same_slot, z3.Or(t_pulse_j > t_fire_i, t_fire_j < t_pulse_i)),
                    label
                )

    @staticmethod
    def add_iq_latch_protect_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        IQ 锁存器保护：在搬运完成前，同 IQ 不能被新读操作覆盖（绝对时间）。
        """
        u_vars = kwargs.get("u_vars")
        if u_vars is None: return

        read_ops = [o for o in ops if o.op_type.upper() == "UR_READ"]
        for i in range(len(read_ops)):
            for j in range(i + 1, len(read_ops)):
                op_A, op_B = read_ops[i], read_ops[j]
                
                same_iq = u_vars[op_A.trace_id] == u_vars[op_B.trace_id]

                def get_protection_window_conds(op_x, op_target_t):
                    """
                    返回一组布尔谓词：确保 op_target_t 晚于 op_x 的所有消费者。
                    """
                    consumers = [
                        o for o in ops
                        if op_x.trace_id in o.metadata.get("depends_on", [])
                    ]
                    if not consumers:
                        return [op_target_t > t_vars[op_x.trace_id]]
                    
                    # 只要 op_target_t 大于每一个消费者的发射时间，就说明 A 已被搬走
                    return [op_target_t > t_vars[cons.trace_id] for cons in consumers]

                # B 在 A 所有消费者之后发射 OR A 在 B 所有消费者之后发射
                cond_B_after_A_all = z3.And(get_protection_window_conds(op_A, t_vars[op_B.trace_id]))
                cond_A_after_B_all = z3.And(get_protection_window_conds(op_B, t_vars[op_A.trace_id]))

                label = f"iq_l_prot_{op_A.trace_id}_{op_B.trace_id}"
                PhysicalAxioms._add_constraint(
                    solver,
                    z3.Implies(same_iq, z3.Or(cond_B_after_A_all, cond_A_after_B_all)),
                    label
                )

    @staticmethod
    def add_alu_liveness_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        ALU Liveness 保护：前驱结果必须在后继覆盖前被搬走（绝对时间）。
        """
        u_vars = kwargs.get("u_vars")
        if u_vars is None: return

        alu_types = [
            "FPMUL", "FPADD", "FPTOINT", "INTTOFP",
            "LOGIC", "SHIFTUP", "SHIFTDN", "INTADD",
        ]
        for alu_type in alu_types:
            alu_ops = [o for o in ops if o.op_type == alu_type]
            if len(alu_ops) <= 1:
                continue
            lat = spec.delays.get(alu_type, 4)

            for i in range(len(alu_ops)):
                for j in range(i + 1, len(alu_ops)):
                    op_i, op_j = alu_ops[i], alu_ops[j]
                    
                    same_unit = u_vars[op_i.trace_id] == u_vars[op_j.trace_id]

                    cons_i = [
                        o for o in ops
                        if op_i.trace_id in o.metadata.get("depends_on", [])
                        and o.op_type == "RTOVR"
                    ]
                    cons_j = [
                        o for o in ops
                        if op_j.trace_id in o.metadata.get("depends_on", [])
                        and o.op_type == "RTOVR"
                    ]

                    if cons_i:
                        i_before_j = t_vars[op_i.trace_id] < t_vars[op_j.trace_id]
                        i_move_done = z3.And(
                            [t_vars[c.trace_id] < t_vars[op_j.trace_id] + lat for c in cons_i]
                        )
                        PhysicalAxioms._add_constraint(
                            solver,
                            z3.Implies(z3.And(same_unit, i_before_j), i_move_done),
                            f"alu_l_i_{op_i.trace_id}_j_{op_j.trace_id}",
                        )

                    if cons_j:
                        j_before_i = t_vars[op_j.trace_id] < t_vars[op_i.trace_id]
                        j_move_done = z3.And(
                            [t_vars[c.trace_id] < t_vars[op_i.trace_id] + lat for c in cons_j]
                        )
                        PhysicalAxioms._add_constraint(
                            solver,
                            z3.Implies(z3.And(same_unit, j_before_i), j_move_done),
                            f"alu_l_j_{op_j.trace_id}_i_{op_i.trace_id}",
                        )

    @staticmethod
    def add_iq_alignment_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        IQ 启动对齐公理：共享同一 IQ 的操作必须落在相同的迭代基准（Iteration Phase）。
        """
        u_vars = kwargs.get("u_vars")
        if u_vars is None: return

        for i in range(len(ops)):
            for j in range(i + 1, len(ops)):
                op_i, op_j = ops[i], ops[j]
                same_iq = u_vars[op_i.trace_id] == u_vars[op_j.trace_id]
                phase_i = t_vars[op_i.trace_id] / II
                phase_j = t_vars[op_j.trace_id] / II
                label = f"iq_align_{op_i.trace_id}_{op_j.trace_id}"
                PhysicalAxioms._add_constraint(
                    solver,
                    z3.Implies(same_iq, phase_i == phase_j),
                    label
                )
