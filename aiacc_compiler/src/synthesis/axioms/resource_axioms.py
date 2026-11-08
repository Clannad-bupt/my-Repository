import z3
from typing import List, Dict, Any, Optional
from ir.mir.mir_op import MIROp
from api.hw_spec import HardwareSpec
from .base import AxiomBase

class ResourceAxioms(AxiomBase):
    """
    模调度冲突模块：处理计算单元、Bank、读写端口在 II 窗口内的模冲突。
    """

    @staticmethod
    def get_remainder_vars(solver, tid, t_vars, II):
        if not hasattr(solver, "remainder_cache"):
            solver.remainder_cache = {}
        if (tid, II) not in solver.remainder_cache:
            q = z3.Int(f"q_{tid}")
            r = z3.Int(f"r_{tid}")
            solver.add(t_vars[tid] == q * II + r, desc=f"rem_eq_{tid}")
            solver.add(z3.And(r >= 0, r < II), desc=f"rem_bound_{tid}")
            solver.remainder_cache[(tid, II)] = (q, r)
        return solver.remainder_cache[(tid, II)]

    @staticmethod
    def add_resource_constraint(solver, ops, t_vars, II, spec, **kwargs):
        """
        通用资源约束入口。
        """
        ResourceAxioms.add_modulo_constraint(solver, ops, t_vars, II, spec, **kwargs)
        # 兼容旧版的其他资源约束
        ResourceAxioms.add_bank_conflict_axiom(solver, ops, t_vars, II, spec, **kwargs)
        ResourceAxioms.add_spill_reload_mutex_axiom(solver, ops, t_vars, II, spec, **kwargs)

    @staticmethod
    def add_modulo_constraint(solver, ops, t_vars, II, spec, **kwargs):
        """
        基础模冲突：同物理单元的操作必须落在不同的模槽。
        """
        u_vars = kwargs.get("u_vars")
        if u_vars is None:
            return

        # 按资源类型分组操作
        type_to_ops: Dict[str, List[MIROp]] = {}
        for op in ops:
            u_type = ResourceAxioms._get_resource_type(op.op_type)
            type_to_ops.setdefault(u_type, []).append(op)

        for u_type, op_list in type_to_ops.items():
            if len(op_list) <= 1:
                continue
            
            # 🛸 [SMT-REFACTOR] 已经剥离了 RTOVR 的 SMT 物理搜索，直接放行
            if u_type == "RTOVR":
                continue
            
            # 过滤掉不存在 u_vars 里的游离节点
            op_list = [op for op in op_list if op.trace_id in u_vars]
            if len(op_list) <= 1:
                continue
            
            # 获取该类型的所有物理实例 ID
            valid_iqs = spec.get_iq_ids(u_type)
            if not valid_iqs:
                continue

            if II <= 6:
                # Pre-create and cache remainder variables once per op to avoid redundant constraint generation
                rem_vars = {}
                for op in op_list:
                    _, r = ResourceAxioms.get_remainder_vars(solver, op.trace_id, t_vars, II)
                    rem_vars[op.trace_id] = r

                for iq_id in valid_iqs:
                    # 对于每一个物理单元 iq_id，每一个模槽 slot，最多只能有一个操作映射上去
                    for slot in range(II):
                        # 构造谓词列表：(op 映射到 iq_id 且 落在 slot 槽位)
                        predicates = []
                        for op in op_list:
                            r = rem_vars[op.trace_id]
                            cond = z3.And(
                                u_vars[op.trace_id] == iq_id,
                                r == slot
                            )
                            predicates.append((cond, 1))
                        
                        label = f"mod_pble_{u_type}_{iq_id}_s{slot}"
                        # 使用 Pseudo-Boolean Le (<= 1) 约束
                        ResourceAxioms._add_constraint(
                            solver,
                            z3.PbLe(predicates, 1),
                            label
                        )
            else:
                # 🛸 [SMT-REFACTOR Step 1.2] 高 II 时，切换到商余数分解与两两互斥
                q_vars = {}
                r_vars = {}
                for op in op_list:
                    tid = op.trace_id
                    q, r = ResourceAxioms.get_remainder_vars(solver, tid, t_vars, II)
                    q_vars[tid] = q
                    r_vars[tid] = r
                
                # 两两互斥：如果两个操作分配在同一个物理单元上，则它们的余数必然不同
                for i in range(len(op_list)):
                    for j in range(i + 1, len(op_list)):
                        op_i, op_j = op_list[i], op_list[j]
                        tid_i, tid_j = op_i.trace_id, op_j.trace_id
                        
                        label = f"mod_gap_{u_type}_{tid_i}_{tid_j}"
                        ResourceAxioms._add_constraint(
                            solver,
                            z3.Implies(
                                u_vars[tid_i] == u_vars[tid_j],
                                r_vars[tid_i] != r_vars[tid_j]
                            ),
                            label
                        )

    @staticmethod
    def _get_resource_type(op_type: str) -> str:
        u = op_type.upper()
        if "UR_READ" in u: return "UR_READ"
        if "UR_WRITE" in u: return "UR_WRITE"
        return u

    @staticmethod
    def add_bank_conflict_axiom(solver, ops=None, t_vars=None, II=None, spec=None, u_vars=None, **kwargs):
        """
        Bank 冲突公理：同一个 Bank 的相同操作类型（读或写）在 II 窗口内不得冲突。
        - 写操作（UR_WRITE）：在同 Bank 内必须严格执行模冲突互斥。
        - 读操作（UR_READ）：只有在逻辑地址不相同时，才需要执行模冲突互斥；相同地址支持广播读取。
        - 🚀 [HARDWARE ALIGNMENT] 显式豁免 stride == 0 的常量/参数读取。
        """
        # 兼容关键字/位置参数调用
        if ops is None:
            ops = kwargs.get("compute_ops", [])

        # 兼容旧签名 (solver, ops, t_vars, s_vars=None) 
        # 如果 II 是字典或 spec 为 None，说明是旧版调用，此时第四个参数为 s_vars
        is_legacy = False
        s_vars = None
        II_val = None
        if isinstance(II, dict) or spec is None:
            is_legacy = True
            s_vars = II
        else:
            II_val = II

        def get_op_bank_and_addr(op) -> tuple:
            bid = None
            addr = op.metadata.get("logical_addr")
            if op.metadata.get("manual_bank") is not None:
                bid = op.metadata["manual_bank"]
            elif op.metadata.get("bank_id") is not None:
                bid = op.metadata["bank_id"]
            elif addr is not None:
                depth = 256
                if spec is not None and hasattr(spec, "params"):
                    depth = getattr(spec.params, "PARAM_UR_BANK_DEPTH", 256)
                bid = addr // depth
            return bid, addr

        def get_mod_val_legacy(op):
            return s_vars[op.trace_id] if s_vars else t_vars[op.trace_id]

        # 按 Bank ID 分组操作
        bank_writes = {} # bank_id -> List[MIROp]
        bank_reads = {}  # bank_id -> List[MIROp]

        for op in ops:
            u_type = ResourceAxioms._get_resource_type(op.op_type)
            bid, addr = get_op_bank_and_addr(op)
            if bid is not None:
                if u_type == "UR_WRITE":
                    bank_writes.setdefault(bid, []).append(op)
                elif u_type == "UR_READ":
                    bank_reads.setdefault(bid, []).append(op)

        if is_legacy:
            # 1. 写冲突公理：同一个 Bank 的任意两个写操作在 II 内必然冲突（必须位于不同模槽）
            for bid, write_ops in bank_writes.items():
                if len(write_ops) > 1:
                    for i in range(len(write_ops)):
                        for j in range(i + 1, len(write_ops)):
                            op_i, op_j = write_ops[i], write_ops[j]
                            label = f"bank_gap_W_{bid}_{op_i.trace_id}_{op_j.trace_id}"
                            ResourceAxioms._add_constraint(
                                solver, 
                                get_mod_val_legacy(op_i) != get_mod_val_legacy(op_j), 
                                label
                            )

            # 2. 读冲突公理：同一个 Bank 的两个读操作，只有当它们逻辑地址不同时，才会在 II 内冲突
            for bid, read_ops in bank_reads.items():
                if len(read_ops) > 1:
                    for i in range(len(read_ops)):
                        for j in range(i + 1, len(read_ops)):
                            op_i, op_j = read_ops[i], read_ops[j]
                            addr_i = op_i.metadata.get("logical_addr")
                            addr_j = op_j.metadata.get("logical_addr")
                            
                            should_conflict = True
                            if addr_i is not None and addr_j is not None:
                                should_conflict = (addr_i != addr_j)
                            
                            if should_conflict:
                                label = f"bank_gap_R_{bid}_{op_i.trace_id}_{op_j.trace_id}"
                                ResourceAxioms._add_constraint(
                                    solver, 
                                    get_mod_val_legacy(op_i) != get_mod_val_legacy(op_j), 
                                    label
                                )
                            else:
                                # 相同物理地址：
                                label = f"bank_gap_R_same_{bid}_{op_i.trace_id}_{op_j.trace_id}"
                                ResourceAxioms._add_constraint(
                                    solver,
                                    z3.Implies(
                                        t_vars[op_i.trace_id] != t_vars[op_j.trace_id],
                                        get_mod_val_legacy(op_i) != get_mod_val_legacy(op_j)
                                    ),
                                    label
                                )
        else:
            # 🚀 [SMT-REFACTOR Step 1.2] 全局消除 % II 原生取模
            conflict_ops = []
            for ops_list in list(bank_writes.values()) + list(bank_reads.values()):
                conflict_ops.extend(ops_list)
            
            # 为这些节点建立 / 复用 Quotient-Remainder (商-余数) 辅助变量
            rem_vars = {}
            for op in set(conflict_ops):
                tid = op.trace_id
                _, r_var = ResourceAxioms.get_remainder_vars(solver, tid, t_vars, II_val)
                rem_vars[tid] = r_var
                
            def get_mod_val(op):
                return rem_vars[op.trace_id]
                
            for bid, write_ops in bank_writes.items():
                if len(write_ops) > 1:
                    for i in range(len(write_ops)):
                        for j in range(i + 1, len(write_ops)):
                            ResourceAxioms._add_constraint(
                                solver, get_mod_val(write_ops[i]) != get_mod_val(write_ops[j]), 
                                f"bank_gap_W_{bid}_{write_ops[i].trace_id}_{write_ops[j].trace_id}"
                            )

            for bid, read_ops in bank_reads.items():
                if len(read_ops) > 1:
                    for i in range(len(read_ops)):
                        for j in range(i + 1, len(read_ops)):
                            addr_i = read_ops[i].metadata.get("logical_addr")
                            addr_j = read_ops[j].metadata.get("logical_addr")
                            if addr_i is None or addr_j is None or addr_i != addr_j:
                                ResourceAxioms._add_constraint(
                                    solver, get_mod_val(read_ops[i]) != get_mod_val(read_ops[j]), 
                                    f"bank_gap_R_{bid}_{read_ops[i].trace_id}_{read_ops[j].trace_id}"
                                )
                            else:
                                # 🚀 [CROSS-ITERATION BANK CONFLICT SOLUTION]
                                # 相同物理 Bank 的相同逻辑地址读取，只有在物理时钟周期完全一致 (t_i == t_j) 时，才能共享/广播读取。
                                # 否则如果落在不同的物理周期 (t_i != t_j)，它们的模槽 (modulo slot) 必须严格互斥，防止跨迭代物理碰撞！
                                ResourceAxioms._add_constraint(
                                    solver,
                                    z3.Implies(
                                        t_vars[read_ops[i].trace_id] != t_vars[read_ops[j].trace_id],
                                        get_mod_val(read_ops[i]) != get_mod_val(read_ops[j])
                                    ),
                                    f"bank_gap_R_same_{bid}_{read_ops[i].trace_id}_{read_ops[j].trace_id}"
                                )

    @staticmethod
    def add_spill_reload_mutex_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        Spill/Reload 互斥：同 Spill 对应的多个 Reload 操作必须落在不同模槽。
        """
        spill_groups: Dict[str, list] = {}
        for op in ops:
            if op.op_type.upper() == "UR_READ":
                for d in op.metadata.get("depends_on", []):
                    if isinstance(d, str) and d.startswith("spill_"):
                        spill_groups.setdefault(d, []).append(op)

        for spill_id, reloads in spill_groups.items():
            if len(reloads) <= 1:
                continue
            for i in range(len(reloads)):
                for j in range(i + 1, len(reloads)):
                    op_i, op_j = reloads[i], reloads[j]
                    label = f"spill_re_{spill_id}_{op_i.trace_id}_{op_j.trace_id}"
                    ResourceAxioms._add_constraint(
                        solver,
                        t_vars[op_i.trace_id] % II != t_vars[op_j.trace_id] % II,
                        label
                    )

    @staticmethod
    def add_symmetry_breaking_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        对称性破缺公理：强制同类资源实例按顺序使用，消除等价排列产生的搜索空间。
        
        策略：对于资源类型 T，如果使用了实例 k，则必须有某个操作使用了实例 k-1。
        这确保了求解器不会跳过实例（例如只用 0 和 2 而不用 1），
        因为在完全对称的情况下，[0, 2] 与 [0, 1] 是等价的。
        """
        u_vars = kwargs.get("u_vars")
        if u_vars is None: return

        # 按类型分组操作
        type_to_ops: Dict[str, List[MIROp]] = {}
        for op in ops:
            u_type = ResourceAxioms._get_resource_type(op.op_type)
            # 只对已知具有物理连接对称性的类型进行破缺 (如 UR 端口)
            # 如果是 ALU，由于 RTOVR 物理绑定可能不对称，不建议开启。
            if u_type in ["UR_READ", "UR_WRITE"]:
                type_to_ops.setdefault(u_type, []).append(op)

        for u_type, op_list in type_to_ops.items():
            valid_iqs = spec.get_iq_ids(u_type)
            if len(valid_iqs) <= 1: continue

            # 对物理实例 ID 进行排序，建立序关系
            sorted_iqs = sorted(valid_iqs)
            
            for k in range(1, len(sorted_iqs)):
                iq_prev = sorted_iqs[k-1]
                iq_curr = sorted_iqs[k]
                
                # 谓词：是否有操作使用了当前实例 iq_curr
                used_curr = z3.Or([u_vars[op.trace_id] == iq_curr for op in op_list])
                # 谓词：是否有操作使用了前一个实例 iq_prev
                used_prev = z3.Or([u_vars[op.trace_id] == iq_prev for op in op_list])
                
                label = f"sym_break_{u_type}_iq{iq_curr}_needs_iq{iq_prev}"
                # 约束：If Used(iq_curr) Then Used(iq_prev)
                ResourceAxioms._add_constraint(
                    solver,
                    z3.Implies(used_curr, used_prev),
                    label
                )

    @staticmethod
    def add_read_port_mutex_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        读端口时分复用。
        """
        # 已经在 add_modulo_constraint 中通过 u_vars 处理了通用的端口/单元复用
        pass

    @staticmethod
    def add_write_port_mutex_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        写端口时分复用。
        """
        # 已经在 add_modulo_constraint 中通过 u_vars 处理了通用的端口/单元复用
        pass

    @staticmethod
    def add_rtovr_modulo_constraint(solver, ops, t_vars, II, spec, **kwargs):
        """
        Enforce that no two RTOVR instructions mapped to the same physical slot can run at the same cycle modulo II.
        """
        u_vars = kwargs.get("u_vars")
        latch_vars = kwargs.get("latch_vars")
        if not latch_vars:
            return

        # 1. Gather all RTOVR variables and their physical slots
        dvr_map = spec.get_dvr_map()
        registry = spec.config.get("iq_registry", {})
        base_w = registry.get("UR_WRITE", 35)

        nodes_by_id = {op.trace_id: op for op in ops}

        # List of tuples: (t_latch_var, slot_expr, consumer_id, prod_id, r_latch_var)
        rt_slots = []

        for (consumer_id, prod_id), t_latch in latch_vars.items():
            consumer = nodes_by_id.get(consumer_id)
            if not consumer:
                continue
            
            # Find the RTOVR node in DFG
            consumer_deps = consumer.metadata.get("depends_on", [])
            rt_node_id = None
            for dep_id in consumer_deps:
                rt_node = nodes_by_id.get(dep_id)
                if rt_node and rt_node.op_type.upper() == "RTOVR":
                    if prod_id in rt_node.metadata.get("depends_on", []):
                        rt_node_id = dep_id
                        break
            if not rt_node_id:
                continue

            i = consumer_deps.index(rt_node_id)
            u_type = consumer.op_type.upper()
            unit_info = spec.modules.get(u_type, {})
            if hasattr(unit_info, "dict"):
                unit_info = unit_info.dict()
            port_names = [p.split("-")[-1] for p in unit_info.get("inputs", [])]
            if not port_names:
                port_names = ["S0", "S1", "S2", "S3", "ST"]
            p_name = port_names[i] if i < len(port_names) else "S0"

            if u_type == "UR_WRITE":
                if u_vars and consumer_id in u_vars:
                    slot_expr = u_vars[consumer_id]
                else:
                    slot_expr = base_w
            else:
                target_slot = 0
                if u_type in dvr_map and p_name in dvr_map[u_type]:
                    target_slot = dvr_map[u_type][p_name]
                slot_expr = target_slot

            # Pre-calculate exact remainder for t_latch using linear Z3 constraints
            q_l = z3.Int(f"q_latch_mod_{consumer_id}_{prod_id}")
            r_l = z3.Int(f"r_latch_mod_{consumer_id}_{prod_id}")
            solver.add(t_latch == q_l * II + r_l)
            solver.add(r_l >= 0, r_l < II)

            rt_slots.append((t_latch, slot_expr, consumer_id, prod_id, r_l))

        # 2. Add mutual exclusion constraints
        for i in range(len(rt_slots)):
            for j in range(i + 1, len(rt_slots)):
                t1, slot1, cons1, prod1, L1 = rt_slots[i]
                t2, slot2, cons2, prod2, L2 = rt_slots[j]

                is_const_equal = False
                if isinstance(slot1, int) and isinstance(slot2, int):
                    if slot1 == slot2:
                        is_const_equal = True
                    else:
                        continue

                if is_const_equal:
                    cond = L1 != L2
                else:
                    cond = z3.Implies(slot1 == slot2, L1 != L2)

                ResourceAxioms._add_constraint(
                    solver,
                    cond,
                    f"rtovr_mod_conflict_{i}_{j}"
                )

    @staticmethod
    def add_rtovr_liveness_axiom(solver, ops, t_vars, II, spec, **kwargs):
        """
        RTOVR 槽位生命期冲突公理 (Modulo-Aware Liveness Mutex):
        在同一个物理 RTOVR 槽位内，各虚拟 RTOVR 的[Latch, Consumer_Fire]生命期在时间轴上不能发生任何重叠。
        """
        u_vars = kwargs.get("u_vars")
        latch_vars = kwargs.get("latch_vars")
        if not latch_vars:
            return

        dvr_map = spec.get_dvr_map()
        registry = spec.config.get("iq_registry", {})
        base_w = registry.get("UR_WRITE", 35)

        nodes_by_id = {op.trace_id: op for op in ops}

        # List of tuples: (t_latch_var, t_fire_var, slot_expr, consumer_id, prod_id, r_latch_var)
        rt_lifespans = []

        for (consumer_id, prod_id), t_latch in latch_vars.items():
            consumer = nodes_by_id.get(consumer_id)
            if not consumer:
                continue
            
            # Find the RTOVR node in DFG
            consumer_deps = consumer.metadata.get("depends_on", [])
            rt_node_id = None
            for dep_id in consumer_deps:
                rt_node = nodes_by_id.get(dep_id)
                if rt_node and rt_node.op_type.upper() == "RTOVR":
                    if prod_id in rt_node.metadata.get("depends_on", []):
                        rt_node_id = dep_id
                        break
            if not rt_node_id:
                continue

            i = consumer_deps.index(rt_node_id)
            u_type = consumer.op_type.upper()
            unit_info = spec.modules.get(u_type, {})
            if hasattr(unit_info, "dict"):
                unit_info = unit_info.dict()
            port_names = [p.split("-")[-1] for p in unit_info.get("inputs", [])]
            if not port_names:
                port_names = ["S0", "S1", "S2", "S3", "ST"]
            p_name = port_names[i] if i < len(port_names) else "S0"

            if u_type == "UR_WRITE":
                if u_vars and consumer_id in u_vars:
                    slot_expr = u_vars[consumer_id]
                else:
                    slot_expr = base_w
            else:
                target_slot = 0
                if u_type in dvr_map and p_name in dvr_map[u_type]:
                    target_slot = dvr_map[u_type][p_name]
                slot_expr = target_slot

            # Pre-calculate exact remainder for t_latch using linear Z3 constraints
            q_l = z3.Int(f"q_latch_live_{consumer_id}_{prod_id}")
            r_l = z3.Int(f"r_latch_live_{consumer_id}_{prod_id}")
            solver.add(t_latch == q_l * II + r_l)
            solver.add(r_l >= 0, r_l < II)

            t_fire = t_vars[consumer_id]
            rt_lifespans.append((t_latch, t_fire, slot_expr, consumer_id, prod_id, r_l))

        # Add periodic overlap exclusion
        for i in range(len(rt_lifespans)):
            for j in range(i + 1, len(rt_lifespans)):
                t_latch_i, t_fire_i, slot_i, cons_i, prod_i, L_i = rt_lifespans[i]
                t_latch_j, t_fire_j, slot_j, cons_j, prod_j, L_j = rt_lifespans[j]

                is_const_equal = False
                if isinstance(slot_i, int) and isinstance(slot_j, int):
                    if slot_i == slot_j:
                        is_const_equal = True
                    else:
                        continue

                D_i = t_fire_i - t_latch_i
                D_j = t_fire_j - t_latch_j

                R_i = L_i + D_i
                R_j = L_j + D_j

                wrap_i = R_i > II
                wrap_j = R_j > II

                is_disjoint = z3.If(
                    z3.And(wrap_i, wrap_j),
                    False,
                    z3.If(
                        wrap_i,
                        z3.And(L_j >= R_i - II, R_j <= L_i),
                        z3.If(
                            wrap_j,
                            z3.And(L_i >= R_j - II, R_i <= L_j),
                            z3.Or(R_i <= L_j, R_j <= L_i)
                        )
                    )
                )

                if is_const_equal:
                    cond = is_disjoint
                else:
                    cond = z3.Implies(slot_i == slot_j, is_disjoint)

                ResourceAxioms._add_constraint(
                    solver,
                    cond,
                    f"rtovr_liveness_overlap_exclude_{cons_i}_{prod_i}_{cons_j}_{prod_j}"
                )

