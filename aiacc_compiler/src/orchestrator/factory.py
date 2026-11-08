from typing import List, Dict
from ir.mir.mir_op import MIROp
from .footprint import FootprintMask

class FootprintFactory:
    """
    足迹工厂：负责将计算图的物理调度结果 (SMT Result) 反射为物理掩码。
    [升级版]：增加了对 Bank 端口访问的跟踪。
    """
    @staticmethod
    def from_smt_result(ops: List[MIROp], result: Dict[str, int]) -> FootprintMask:
        mask = FootprintMask()
        for op in ops:
            t = result.get(op.node_id)
            if t is None: continue
            
            # 1. ALU 核心占位
            if op.op_type in ["FPMUL", "FPADD"]:
                iq = op.metadata.get("iq_id")
                if iq: mask.add_resource(t, f"iq_{iq}")
            
            # 2. 路由槽位占位
            elif op.op_type == "RTOVR":
                slot = op.metadata.get("slot_id")
                if slot is not None: mask.add_resource(t, f"slot_{slot}")
                
            # 3. Bank 端口以及 IQ 端口占位
            elif op.op_type in ["UR_READ", "UR_WRITE"]:
                # 跟踪 IQ 物理口
                iq = op.metadata.get("iq_id")
                if iq: mask.add_resource(t, f"iq_{iq}")
                
                # [关键新逻辑] 跟踪 Bank 物理口
                # 假设 Bank ID 存储在元数据 metadata['bank_id'] 中
                bank_id = op.metadata.get("bank_id")
                if bank_id is not None:
                    # 区分读写端口以支持更高的并行度 (如果硬件支持读写独立)
                    mode = "rd" if op.op_type == "UR_READ" else "wr"
                    mask.add_resource(t, f"bank_{bank_id}_{mode}")
                
        return mask
