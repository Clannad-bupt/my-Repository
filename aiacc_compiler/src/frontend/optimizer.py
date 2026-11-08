from typing import List, Dict, Any, Set, Optional
from ir.mir.mir_op import MIROp
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

class PhysicalGraphOptimizer:
    """
    AIACC 物理图优化器 (Engineering Grade).
    主要职责：
    1. 生命周期审计：检测高扇出脉冲节点。
    2. 自动溢出 (Auto-Spill)：在脉冲消失前将数据存入 UR Bank。
    3. 资源预分配：管理编译器内部使用的临时 Scratchpad Bank。
    """
    def __init__(self, scratch_bank: int = 6, bank_depth: int = 256,
                 layout: Optional[Any] = None):
        self.scratch_bank = scratch_bank
        self.bank_depth = bank_depth
        self.scratch_offset = 0
        # [Phase 2] 统一地址仲裁者。若注入了 layout，则所有地址通过它分配
        self._layout = layout
        # [Phase 6] 标量运算图直接豁免溢出保护
        self.disable_spill = getattr(self, "disable_spill", False)

    def _alloc_scratch(self, name: str) -> int:
        """统一 scratch 地址分配入口。优先使用 layout，回退自算。"""
        if self._layout is not None:
            return self._layout.allocate_scratch(name)
        # 回退：保持原有行为
        addr = self.scratch_bank * self.bank_depth + self.scratch_offset
        self.scratch_offset += 1
        return addr

    def optimize(self, dfg: List[MIROp]) -> List[MIROp]:
        """
        执行物理图优化全流程。
        """
        if getattr(self, "disable_spill", False):
            return dfg

        # 1. 构建依赖反向索引 (Consumer Map)
        consumers = self._build_consumer_map(dfg)
        
        # 2. 识别需要溢出的高扇出计算节点
        spill_targets = []
        for op in dfg:
            # 🛸 [ENGINEERING] 所有产生瞬时脉冲的节点 (Compute + Memory Read) 均需受到保护
            is_pulse = op.op_type.upper() in [
                "FPMUL", "FPADD", "LOGIC", "INTADD", "FPTOINT", "INTTOFP",
                "UR_READ" # UR_READ 产生的是从 Bank 到总线的脉冲
            ]
            if is_pulse and len(consumers.get(op.trace_id, [])) > 1:
                spill_targets.append(op)

        if not spill_targets:
            return dfg

        # 3. 执行溢出变换
        return self._apply_spill_transform(dfg, spill_targets, consumers)

    def _build_consumer_map(self, dfg: List[MIROp]) -> Dict[str, List[MIROp]]:
        consumers = {}
        for op in dfg:
            deps = op.metadata.get("depends_on", [])
            for dep_id in deps:
                if dep_id not in consumers:
                    consumers[dep_id] = []
                consumers[dep_id].append(op)
        return consumers

    def _apply_spill_transform(self, dfg: List[MIROp], targets: List[MIROp], consumers: Dict[str, List[MIROp]]) -> List[MIROp]:
        """
        将目标节点替换为 Spill-Reload 序列。
        """
        new_builder = MIROpBuilder()
        processed_dfg = []
        
        # 映射表：Original Producer ID -> Reloaded Node ID
        reload_map = {}
        
        # 记录已处理的节点，保持拓扑顺序
        target_ids = {t.trace_id for t in targets}
        
        for op in dfg:
            # 将原始节点加入新图
            new_builder.ops.append(op)
            
            if op.trace_id in target_ids:
                if op.op_type.upper() == "UR_READ":
                    # 🛸 [ENGINEERING] 对于访存节点，直接采取“多路复读”策略，避免占用额外的 Spill Bank
                    for i, consumer in enumerate(consumers[op.trace_id]):
                        if i == 0: continue # 第一个消费者复用原节点
                        
                        dup_id = f"{op.trace_id}_dup{i}"
                        new_builder.make_op(op.op_type, dup_id, op.metadata["iq_type"], 
                                            op.metadata["depends_on"], 
                                            **{k:v for k,v in op.metadata.items() if k not in ["depends_on", "iq_type"]})
                        
                        # 记录这个消费者的重定向映射
                        if op.trace_id not in reload_map: reload_map[op.trace_id] = {}
                        reload_map[op.trace_id][consumer.trace_id] = dup_id
                else:
                    # 🛸 [ENGINEERING] 对于计算节点，插入 Spill-Reload 序列
                    spill_id = f"spill_{op.trace_id}"
                    # [Phase 2] 通过统一仲裁者分配 scratch 地址
                    absolute_addr = self._alloc_scratch(op.trace_id)
                    actual_scratch_bank = absolute_addr // self.bank_depth
                    inc = op.metadata.get("inc", 0)
                    rtovr_id = f"rt_spill_{op.trace_id}"
                    new_builder.make_op("RTOVR", rtovr_id, PS.UR_W0_S, [op.trace_id])

                    new_builder.make_op("UR_WRITE", spill_id, RT.UR_WRITE, [rtovr_id],
                                        bank_id=actual_scratch_bank,
                                        logical_addr=absolute_addr,
                                        role="intermediate",
                                        inc=inc)

                    # 为每个消费者产生一个独立的 Reload 节点
                    for i, consumer in enumerate(consumers[op.trace_id]):
                        reload_id = f"reload_{op.trace_id}_c{i}"
                        new_builder.make_op("UR_READ", reload_id, RT.UR_READ, [spill_id],
                                            bank_id=actual_scratch_bank,
                                            logical_addr=absolute_addr,
                                            role="intermediate",
                                            inc=inc)
                        
                        if op.trace_id not in reload_map: reload_map[op.trace_id] = {}
                        reload_map[op.trace_id][consumer.trace_id] = reload_id

        # 🛸 [ENGINEERING] 精准重定向
        for op in new_builder.ops:
            deps = op.metadata.get("depends_on", [])
            new_deps = []
            changed = False
            for dep_id in deps:
                if dep_id in reload_map and op.trace_id in reload_map[dep_id]:
                    new_deps.append(reload_map[dep_id][op.trace_id])
                    changed = True
                else:
                    new_deps.append(dep_id)
            if changed:
                op.metadata["depends_on"] = new_deps

        return new_builder.get_dfg()
