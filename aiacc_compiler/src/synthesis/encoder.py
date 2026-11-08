from dataclasses import dataclass
from typing import Dict, List

@dataclass
class SMIHeader:
    """SMI_IQ 共有 Header"""
    vld: int = 1
    jump: int = 0
    embed: int = 0
    embed_end: int = 0
    loops: int = 0
    cond: int = 0
    dly: int = 0
    inc: int = 0
    inc_embed: int = 0

    def to_string(self) -> str:
        fields = [
            f"VALID:{self.vld}", f"JUMP:{self.jump}", f"EMBED:{self.embed}",
            f"EMBED_END:{self.embed_end}", f"LOOPS:{self.loops}",
            f"COND:{self.cond}", f"DLY:{self.dly}", f"INC:{self.inc}",
            f"INC_EMBED:{self.inc_embed}"
        ]
        return "HEAD:[" + "|".join(fields) + "]"

class SMIEncoderTruth:
    """
    真相对齐编码器：按硬件单元 Dump 所有指令及 DLY 时序。
    """
    def __init__(self, ii: int):
        self.ii = ii

    def get_body_string(self, op_type: str, trace_id: str, ops: List, unit_map: Dict) -> str:
        """根据算子类型生成 Mock 的 BODY 字段"""
        op_type = op_type.upper()
        if "FPADD" in op_type:
            return "BODY:[SRC_PREC:1|DST_PREC:1|UP_STAT_EN:0|ADD_OR_SUB:0|SMC_ID_EN:31]"
        if "FPMUL" in op_type:
            return "BODY:[SRC_PREC:1|DST_PREC:1|SMC_ID_EN:31]"
        if "RTOVR" in op_type:
            # 🛸 动态解析：查找该 RTOVR 的前驱算子所在的物理单元
            this_op = next((o for o in ops if o.trace_id == trace_id), None)
            if this_op:
                deps = this_op.metadata.get("depends_on", [])
                if deps:
                    producer_id = deps[0]
                    p_unit = unit_map.get(producer_id, "unknown")
                    # 假设端口映射：FPMUL -> 31, FPADD -> 32, UR_READ -> 33
                    if "FPMUL" in p_unit.upper(): sel = 31
                    elif "FPADD" in p_unit.upper(): sel = 32
                    elif "UR_READ" in p_unit.upper(): sel = 33
                    else: sel = 0
                    return f"BODY:[SEL:{sel}]"
            return "BODY:[SEL:3]" # Fallback
        if "UR_READ" in op_type:
            return f"BODY:[BANK:0|ADDR:0|SMC_ID_EN:31]"
        if "UR_WRITE" in op_type:
            return f"BODY:[BANK:3|ADDR:0|WBE:ffff|SMC_ID_EN:31]"
        return "BODY:[RAW_CODE:0]"

    def dump_truth_aligned(self, ops: List, schedule: Dict[str, int], 
                           unit_map: Dict[str, str], loop_count: int, base_stride: int = 1) -> str:
        lines = ["TRUTH-ALIGNED MICRO-INSTRUCTION DUMP", "="*60]
        
        # 1. 收集每个单元的所有指令
        unit_instructions = {}
        for op in ops:
            u_name = unit_map[op.trace_id]
            unit_instructions.setdefault(u_name, []).append(op)
        
        dynamic_inc = base_stride * self.ii
        
        # 2. 按单元输出序列
        for u_name in sorted(unit_instructions.keys()):
            display_name = u_name.replace("-", "_")
            ops_in_unit = unit_instructions[u_name]
            
            # 🛸 关键优化：计算该单元的最小迭代偏移 (k_min)
            # 我们通过在循环前插入一个 NOP + loops 来实现“初始相位对齐”
            k_min = min(schedule[op.trace_id] // self.ii for op in ops_in_unit)
            
            if k_min > 0:
                h_init = SMIHeader(vld=0, loops=(k_min * self.ii) - 1)
                lines.append(f"{display_name}[INIT] : {h_init.to_string()} | BODY:[NOP] /* Initial Phase Offset: {k_min} iterations */")
            
            # --- [插入 Header 控制行: EMBED=1] ---
            h_header = SMIHeader(vld=1, embed=1, loops=loop_count - 1)
            lines.append(f"{display_name}[HDR]  : {h_header.to_string()} | BODY:[LOOP_HEADER_CONFIG]")
            
            # --- [插入 Body 执行块: 强制长度等于 II] ---
            for s in range(self.ii):
                # 寻找该 Slot (s) 是否有对应的操作
                target_op = None
                for op in ops_in_unit:
                    if (schedule[op.trace_id] % self.ii) == s:
                        target_op = op
                        break
                
                h = SMIHeader()
                h.loops = 0
                h.embed = 0
                
                if s == self.ii - 1:
                    h.embed_end = 1
                
                if target_op:
                    h.vld = 1
                    # 🛸 关键简化：时序已在 INIT NOP 中对齐，循环体内的指令不再需要 DLY。
                    # 模调度保证了在一个 II 窗口内，所有指令按 Slot (s) 顺序发射即可。
                    h.dly = 0
                    h.inc = base_stride if not target_op.metadata.get("is_invariant") else 0
                    body = self.get_body_string(target_op.op_type, target_op.trace_id, ops, unit_map)
                    label = f"Op: {target_op.trace_id:<8}"
                else:
                    h.vld = 0
                    h.dly = 0
                    h.inc = 0
                    body = "BODY:[NOP]"
                    label = "NOP"
                
                lines.append(f"{display_name}[{s}]  : {h.to_string()} | {body} /* {label} */")
        
        return "\n".join(lines)
