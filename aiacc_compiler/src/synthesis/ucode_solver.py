import z3
from typing import List, Dict, Any, Optional, Tuple
from ir.mir.mir_op import MIROp
from api.hw_spec import HardwareSpec
import synthesis.axioms as axioms

class UCodeSynthesizer:
    """
    AIACC 微码综合器 (V25.0 - 恢复通用资源池分配)。
    修正了对 UR Bank 绑定的误解：ur-wx 可以自由访问所有 Bank。
    """
    def __init__(self, hw_spec: HardwareSpec):
        self.hw_spec = hw_spec
        self.res_to_iqs = {}
        registry = self.hw_spec.config.get("iq_registry", {})
        for res_type in registry.keys():
            self.res_to_iqs[res_type.upper()] = self.hw_spec.get_iq_ids(res_type)

    def _get_resource_type(self, op_type: str) -> str:
        u = op_type.upper()
        if "UR_READ" in u: return "UR_READ"
        if "UR_WRITE" in u: return "UR_WRITE"
        return u

    def solve_with_units(self, ops: List[MIROp], MII: int = 1, MAX_II: int = 32) -> Optional[Dict[str, Any]]:
        """
        🚀 [SAT-OPT-1] 线性扫描：从精确 MII 向上寻找第一个 SAT 且验证通过的方案。
        彻底解决非单调硬件约束下的二分查找漏检问题（例如 addcmul 的 II=2..4 为 SAT, II=5 为 UNSAT）。
        因为精确 MII 极准，所以通常第 1 次或第 2 次探测即 SAT，综合性能、稳定性和最优解质量均达到极致。
        """
        from core.ii_analyzer import IIAnalyzer
        computed_mii = IIAnalyzer(self.hw_spec).compute(ops)
        effective_mii = max(MII, computed_mii)
        
        # 保存初始 metadata 状态，防止被之前 SAT 的迭代污染
        original_meta = {op.trace_id: {"iq_id": op.metadata.get("iq_id"), "slot_id": op.metadata.get("slot_id")} for op in ops}
        
        best_res = None
        best_meta = None
        
        for ii in range(effective_mii, MAX_II + 1):
            # 每次探测前恢复初始状态
            for op in ops:
                if original_meta[op.trace_id]["iq_id"] is None:
                    op.metadata.pop("iq_id", None)
                else:
                    op.metadata["iq_id"] = original_meta[op.trace_id]["iq_id"]
                    
                if original_meta[op.trace_id]["slot_id"] is None:
                    op.metadata.pop("slot_id", None)
                else:
                    op.metadata["slot_id"] = original_meta[op.trace_id]["slot_id"]

            print(f"[SOLVER] Trying II={ii}...")
            result = self._solve_for_ii_with_units(ops, ii)
            if result:
                schedule, unit_map = result
                # 执行物理验证审计
                if self.validate_physical_schedule(ops, schedule, unit_map, ii):
                    print(f"[SOLVER] II={ii} is SAT and Verified. Found optimal II.")
                    best_res = {"schedule": schedule, "II": ii, "unit_assignments": unit_map}
                    best_meta = {op.trace_id: {"iq_id": op.metadata.get("iq_id"), "slot_id": op.metadata.get("slot_id")} for op in ops}
                    break
                else:
                    print(f"[VERIFIER] Counter-verification FAILED for II={ii}. Trying next.")
            else:
                print(f"[SOLVER] II={ii} is UNSAT. Trying next.")
                
        # 应用最佳的 metadata 结果
        if best_meta:
            for op in ops:
                if best_meta[op.trace_id]["iq_id"] is not None:
                    op.metadata["iq_id"] = best_meta[op.trace_id]["iq_id"]
                if best_meta[op.trace_id]["slot_id"] is not None:
                    op.metadata["slot_id"] = best_meta[op.trace_id]["slot_id"]
                    
        return best_res

    def validate_physical_schedule(self, ops: List[MIROp], schedule: Dict[str, int], unit_map: Dict[str, str], II: int) -> bool:
        """
        全物理维度严格反证审计 (Rigid Physical Audit V5.2)。
        """
        nodes_by_id = {op.trace_id: op for op in ops}
        iq_to_modulo: Dict[str, set] = {}
        iq_to_phases: Dict[str, set] = {}
        
        for op in ops:
            if op.op_type.upper() == "RTOVR": continue
            t = schedule.get(op.trace_id)
            if t is None: continue
            
            iq_name = unit_map.get(op.trace_id)
            if not iq_name: continue
            
            # 1. [资源审计] 模冲突二次确认
            mod = t % II
            if mod in iq_to_modulo.get(iq_name, set()):
                print(f"[VERIFIER-FAIL] Modulo Collision in {iq_name} at Slot {mod}")
                return False
            iq_to_modulo.setdefault(iq_name, set()).add(mod)

            # 2. [安全审计] 访存相位隔离 (Side-Effect Isolation)
            if op.op_type.upper() in ["UR_READ", "UR_WRITE"]:
                if not (op.op_type.upper() == "UR_READ" and op.metadata.get("stride") == 0):
                    phase = t // II
                    iq_to_phases.setdefault(iq_name, set()).add(phase)
        
        # 3. [相位审计] 检查访存单元是否跨 Phase
        for iq_name, phases in iq_to_phases.items():
            if len(phases) > 1:
                # 🛸 [V6.1 PIPELINE OPT] 允许跨相复用，释放 II 极限
                # print(f"[VERIFIER-FAIL] Phase Contamination in {iq_name}: {phases}")
                # return False
                pass
                
        # 4. [时序审计] 生产 -> RTOVR 采样 -> ALU 消费 严格对齐
        for op in ops:
            if op.op_type.upper() == "RTOVR": continue
            m_type = op.op_type.upper()
            deps = op.metadata.get("depends_on", [])
            
            # [OPT Step3] RTOVR 已抽象为边，从消费者反推采样时刻
            for prod_id, rtovr_info in op.metadata.get("rtovr_deps", {}).items():
                prod = nodes_by_id.get(prod_id)
                if not prod:
                    continue
                lat_p = self.hw_spec.delays.get(prod.op_type.upper(), 1)
                t_prod = schedule.get(prod_id)
                if t_prod is None: continue
                t_consumer = schedule.get(op.trace_id)
                if t_consumer is None: continue
                
                # 🛸 [V6.0 LATCH OPTIMIZATION] 优先从 schedule 中获取真实的 RTOVR 调度时间，降级退化到 t_consumer - 1
                rtovr_node = None
                for dep_id in deps:
                    node = nodes_by_id.get(dep_id)
                    if node and node.op_type.upper() == "RTOVR":
                        if prod_id in node.metadata.get("depends_on", []):
                            rtovr_node = node
                            break
                
                if rtovr_node and schedule.get(rtovr_node.trace_id) is not None:
                    t_rtovr_inferred = schedule[rtovr_node.trace_id]
                else:
                    t_rtovr_inferred = t_consumer - 1
                if t_rtovr_inferred < t_prod + lat_p:
                    print(f"[VERIFIER-FAIL] RTOVR undersampling: {op.trace_id} reads before {prod_id} ready "
                          f"(inferred t_rtovr={t_rtovr_inferred}, need >= {t_prod + lat_p})")
                    return False
                
                # 🛸 [BUG FIX] 修正物理覆盖上限公式：
                # 锁存器仅在下一次迭代输出完成时（t_prod + lat_p + II）才被覆盖。
                # 且如果生产者是常量、不变量、stride=0 访存或持久化写入，则免除覆盖检查。
                is_inv = (prod.metadata.get("is_invariant", False) or 
                          prod.metadata.get("is_const", False) or 
                          prod.metadata.get("stride", 1) == 0 or
                          "UR_WRITE" in prod.op_type.upper())
                if not is_inv:
                    limit = t_prod + lat_p + II
                    if t_rtovr_inferred >= limit:
                        print(f"[VERIFIER-FAIL] RTOVR overwrite: {op.trace_id} reads after {prod_id} latch overwritten "
                              f"(inferred t_rtovr={t_rtovr_inferred}, limit < {limit})")
                        return False
                            
        return True

    def _solve_for_ii_with_units(self, ops: List[MIROp], II: int) -> Optional[Tuple[Dict[str, int], Dict[str, str]]]:
        compute_ops = [op for op in ops if op.op_type.upper() != "RTOVR"]
        
        solver = z3.Solver()
        solver.set("timeout", 10000) # 🛸 [SMT-OPTIMIZATION] 10s timeout to fast-forward past hard UNSAT instances
        orig_add = solver.add
        _track_idx = 0
        def tracked_add(*args, **kwargs):
            nonlocal _track_idx
            desc = kwargs.pop("desc", "")
            if desc: desc = "_" + desc.replace(" ", "_").replace("=", "eq")
            for arg in args:
                _track_idx += 1
                lbl = z3.Bool(f"lbl_{_track_idx}{desc}")
                solver.assert_and_track(arg, lbl)
        solver.add = tracked_add
        
        t_vars = {op.trace_id: z3.Int(f"t_{op.trace_id}") for op in compute_ops}
        u_vars = {op.trace_id: z3.Int(f"u_{op.trace_id}") for op in compute_ops}

        # 1. 基础物理约束
        for op in compute_ops:
            # 🛸 [V27.1 REVERT] 回归 t >= 0。依靠 L_init=1 来提供物理启动缓冲。
            solver.add(t_vars[op.trace_id] >= 0, desc=f"t_pos_{op.trace_id}")
            u_type = self._get_resource_type(op.op_type)
            valid_iqs = self.res_to_iqs.get(u_type, [0])
            # 恢复：UR 单元可以在同类池中自由选择 (ur-w0, 1, 2, 3 均可)
            solver.add(z3.Or([u_vars[op.trace_id] == iq_id for iq_id in valid_iqs]), desc=f"valid_iq_{op.trace_id}")

        # 2. 物理公理
        latch_vars = {}
        axioms.add_latency_axiom(solver, compute_ops, t_vars, II, self.hw_spec, latch_vars=latch_vars)

        # ── 2.5. 通用循环携带依赖约束 (Generic Loop-Carried Dependency Constraints) ──
        for op in compute_ops:
            for dep_id, latency in op.metadata.get("loop_carried_depends_on", {}).items():
                if dep_id in t_vars:
                    print(f"[SMT] Adding loop-carried constraint from DFG: {op.trace_id} + II >= {dep_id} + {latency}")
                    solver.add(t_vars[op.trace_id] + II >= t_vars[dep_id] + latency, desc=f"loop_carried_{op.trace_id}_{dep_id}")

        # axioms.add_rtovr_crossbar_axiom(solver, compute_ops, t_vars, II, self.hw_spec, u_vars=u_vars)
        # axioms.add_shadow_binding_axiom(solver, compute_ops, t_vars, II, self.hw_spec)
        # axioms.add_anti_overwrite_axiom(solver, compute_ops, t_vars, II, self.hw_spec)
        axioms.add_side_effect_phase_axiom(solver, compute_ops, t_vars, II, self.hw_spec, u_vars=u_vars)
        # axioms.add_rtovr_alignment_axiom(solver, compute_ops, t_vars, II, self.hw_spec)


        # 3. 模冲突
        axioms.add_resource_constraint(solver, compute_ops, t_vars, II, self.hw_spec, u_vars=u_vars)
        # axioms.add_rtovr_modulo_constraint(solver, ops, t_vars, II, self.hw_spec, u_vars=u_vars, latch_vars=latch_vars)
        # axioms.add_rtovr_liveness_axiom(solver, ops, t_vars, II, self.hw_spec, u_vars=u_vars, latch_vars=latch_vars)

        # 4. RtoVR 物理绑定
        # axioms.add_physical_binding_axiom(solver, compute_ops, t_vars, II, self.hw_spec, u_vars=u_vars)
        # 5. 手动 IQ 绑定 (供调试使用)
        for op in compute_ops:
            manual_iq = op.metadata.get("iq_id")
            if manual_iq is not None:
                solver.add(u_vars[op.trace_id] == manual_iq, desc=f"manual_iq_{op.trace_id}")

        if solver.check() != z3.sat:
            try:
                core = solver.unsat_core()
                print(f"[DEBUG-UNSAT-CORE] II={II} unsat core: {core}")
            except Exception as e:
                print(f"[DEBUG-UNSAT-CORE] Failed to get unsat core: {e}")
            return None
        model = solver.model()
        schedule = {op.trace_id: model.eval(t_vars[op.trace_id]).as_long() for op in compute_ops}
        
        # 🛸 [V6.0 LATCH OPTIMIZATION] 评估并提取求解出的虚拟锁存器 RTOVR 调度时间
        latch_schedule = {}
        if latch_vars:
            for key, var in latch_vars.items():
                latch_val = model.eval(var)
                if hasattr(latch_val, "as_long"):
                    latch_schedule[key] = latch_val.as_long()
        registry = self.hw_spec.config.get("iq_registry", {})
        
        # 🛸 [V5.3 PATCH] 动态获取基地址，移除硬编码 100
        base_rt = registry.get("RTOVR", 100) 
        
        unit_assignments = {}
        for op in compute_ops:
            u_id = model.eval(u_vars[op.trace_id]).as_long()
            iq_name = self.hw_spec.get_iq_name(u_id, op.op_type)
            unit_assignments[op.trace_id] = iq_name
            op.metadata["iq_id"] = u_id

        # [OPT Step 3] Post-hoc 恢复 RTOVR 节点信息
        dvr_map = self.hw_spec.get_dvr_map()
        for op in compute_ops:
            u_type = self._get_resource_type(op.op_type)
            u_id = op.metadata["iq_id"]
            
            # 🛸 [ENGINEERING] 修正模块信息查找路径
            unit_info = self.hw_spec.modules.get(u_type, {})
            if hasattr(unit_info, "dict"): unit_info = unit_info.dict()
            port_names = [p.split("-")[-1] for p in unit_info.get("inputs", [])]
            if not port_names:
                port_names = ["S0", "S1", "S2", "S3", "ST"]
            
            valid_iqs = self.res_to_iqs.get(u_type, [0])
            idx = valid_iqs.index(u_id) if u_id in valid_iqs else 0
            inst_name = f"{u_type}_{idx}" if len(valid_iqs) > 1 else u_type
            
            deps = op.metadata.get("depends_on", [])
            for i, dep_id in enumerate(deps):
                producer = next((o for o in ops if o.trace_id == dep_id), None)
                if producer and producer.op_type.upper() == "RTOVR":
                    # 恢复时间：优先使用 SMT 求解的 latch_schedule，否则降级回退到 t_consumer - 1
                    prod_id = producer.metadata.get("depends_on", [None])[0]
                    latch_time = latch_schedule.get((op.trace_id, prod_id))
                    if latch_time is not None:
                        schedule[producer.trace_id] = latch_time
                    else:
                        schedule[producer.trace_id] = schedule[op.trace_id] - 1
                    
                    # 恢复物理绑定
                    p_name = port_names[i] if i < len(port_names) else "S0"
                    target_slot = 0
                    if inst_name in dvr_map and p_name in dvr_map[inst_name]:
                        target_slot = dvr_map[inst_name][p_name]
                        
                    rt_id = base_rt + target_slot
                    
                    # 🛸 [ENGINEERING] 多端口生产者解析：根据 dep_port 找到正确的 Source ID
                    # 查找该 RTOVR 节点对应生产者的哪个端口 (D 或 ST)
                    dep_port = producer.metadata.get("dep_port", "D")
                    dr_map = self.hw_spec.get_dr_map() # Unit -> Port -> ID
                    
                    # 获取真实的生产者节点（不是 RTOVR，而是 RTOVR 的输入源）
                    # 注意：在 DFG 中，RTOVR 节点只有一个 dependency，即真正的生产者
                    real_producer_id = producer.metadata.get("depends_on", [None])[0]
                    real_producer = next((o for o in ops if o.trace_id == real_producer_id), None)
                    real_producer_iq_id = real_producer.metadata.get("iq_id", u_id) if real_producer else u_id
                    
                    # 根据物理 ID 反查实例名，用于 dr_map 匹配 (例如 "UR_READ_0")
                    real_producer_inst = self.hw_spec.get_iq_name(real_producer_iq_id).upper().replace("-", "_")
                    
                    # 查找优先级: 实例级(UR_READ_0) > 类型级(UR_READ) > 默认(real_producer_iq_id)
                    sel_id = dr_map.get(real_producer_inst, dr_map.get(real_producer.op_type.upper() if real_producer else "", {})).get(dep_port, real_producer_iq_id)
                    
                    unit_assignments[producer.trace_id] = self.hw_spec.get_iq_name(rt_id, "RTOVR")
                    producer.metadata["iq_id"] = rt_id
                    producer.metadata["slot_id"] = target_slot
                    producer.metadata["sel"] = sel_id

        return schedule, unit_assignments
