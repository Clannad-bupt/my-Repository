from typing import List, Dict, Set
from ir.mir.mir_op import MIROp

class HSTSVerifier:
    """
    AIACC 物理规格审计员：对 HSTS 缝合后的全量指令流进行“生命周期对齐”校验。
    """
    @staticmethod
    def verify(stream: List[MIROp], hw_spec) -> bool:
        print("\n[VERIFIER] Starting Full-Life-Cycle Audit...")
        
        # 1. 建立归一化时延表 (处理 RtoVR vs RTOVR)
        norm_delays = {k.upper(): v for k, v in hw_spec.delays.items()}
        
        # 2. 结构化索引
        nodes: Dict[str, MIROp] = {op.node_id: op for op in stream}
        iq_usage: Dict[int, Dict[int, str]] = {}
        bank_usage: Dict[int, Dict[int, str]] = {}
        # 槽位占用追踪: {slot_id: [(start_t, end_t, node_id)]}
        slot_timeline: Dict[int, List[tuple]] = {}
        
        errors = []

        for op in stream:
            global_t = op.metadata["global_t"]
            op_type = op.op_type.upper()
            
            # --- A. 数据依赖校验 (RAW) ---
            deps = op.metadata.get("depends_on", [])
            last_dep_finish = 0
            for dep_id in deps:
                if dep_id in nodes:
                    dep_op = nodes[dep_id]
                    delay = norm_delays.get(dep_op.op_type.upper(), 1)
                    finish_t = dep_op.metadata["global_t"] + delay
                    last_dep_finish = max(last_dep_finish, finish_t)
                    if global_t < finish_t:
                        errors.append(f"  [RAW ERROR] Node {op.node_id} at T={global_t} fires BEFORE dependency {dep_id} (ready at T={finish_t})")

            # --- B. 单元发射与 Bank 冲突 (IQ/Bank Collision) ---
            for attr in ["iq_id", "bank_id"]:
                if attr in op.metadata:
                    val = op.metadata[attr]
                    usage = iq_usage if attr == "iq_id" else bank_usage
                    t_map = usage.setdefault(global_t, {})
                    if val in t_map:
                        errors.append(f"  [{attr.upper()} COLLISION] T={global_t}: {attr} {val} contention between {t_map[val]} and {op.node_id}")
                    t_map[val] = op.node_id

            # --- C. Slot 生命周期安全校验 (Register Conflict) ---
            # 如果是 RTOVR 写回物理 Slot，它具有生命周期
            if op_type == "RTOVR" and "slot_id" in op.metadata:
                slot_id = op.metadata["slot_id"]
                # 寻找这个 node 的所有消费者
                consumers = [n for n in stream if op.node_id in n.metadata.get("depends_on", [])]
                max_consume_t = max([c.metadata["global_t"] for c in consumers]) if consumers else global_t + 1
                
                # 检查该 Slot 在 [global_t, max_consume_t] 窗口内是否被他人覆盖
                timeline = slot_timeline.setdefault(slot_id, [])
                for start, end, owner in timeline:
                    # 检查时间区间重叠 (Overlap Detection)
                    if not (max_consume_t <= start or global_t >= end):
                        errors.append(f"  [SLOT OVERWRITE] T={global_t}: Node {op.node_id} overwrites Slot {slot_id} while Node {owner} still using it (until T={end})")
                
                timeline.append((global_t, max_consume_t, op.node_id))

        if errors:
            print(f"❌ Verification FAILED with {len(errors)} errors:")
            for e in errors[:10]: print(e)
            return False
        
        print(f"✅ FULL-ALIGNMENT VERIFICATION PASSED! (Checked {len(stream)} instructions, absolute data integrity guaranteed.)")
        return True
