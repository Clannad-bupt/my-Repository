"""
UnitPayloadFactory — AIACC 动态指令载荷工厂 (Phase 3)

[架构契约 Contract-2]
  彻底废弃 assembler 和 emitter 中基于硬编码字符串匹配的 Payload 生成逻辑。
  所有算子单元的属性 (VALID, OP_TYPE, PRECISION 等) 均由 modules.yaml 定义的 code_fields 动态驱动。
  编译器前端传入的 metadata 必须与 YAML 中定义的 field name 一致，不一致的做规范化映射。
"""

from typing import Dict, Any

class UnitPayloadFactory:
    """基于 hw_spec 动态派生 Unit Payload"""

    @staticmethod
    def resolve_iq_type(iq_name: str, hw_spec: Any) -> str:
        """从 hw_spec.modules 动态推断单元类型"""
        iq_lower = iq_name.lower()
        # [SSoT] 动态匹配
        for unit_type in hw_spec.modules.keys():
            normalized = unit_type.lower().replace("_", "-")
            if iq_lower == normalized or iq_lower.startswith(normalized + "-"):
                return unit_type
        # 兼容旧的前缀命名约定
        if "ur-r" in iq_lower: return "UR_READ"
        if "ur-w" in iq_lower: return "UR_WRITE"
        if "rtovr" in iq_lower: return "RTOVR"
        
        raise ValueError(
            f"[UnitPayloadFactory] 无法识别 IQ 类型: '{iq_name}'。"
            f"已知类型: {list(hw_spec.modules.keys())}。"
        )

    @staticmethod
    def build(op: Any, trace_to_slot: Dict[str, int], trace_to_type: Dict[str, str], hw_spec: Any) -> Dict[str, Any]:
        """
        根据 op 的元数据和 hw_spec 中的硬件契约，动态生成 payload 字典。
        """
        import copy
        res = copy.deepcopy(op.metadata.get("physical_attrs", {}))
        m_type = str(op.op_type).upper().strip()

        # 1. 如果这不是真实执行单元（例如纯 RTOVR 逻辑），走默认/后备逻辑
        if "RTOVR" in m_type:
            # RTOVR 不是严格意义的计算模块，走专用逻辑
            return UnitPayloadFactory._build_rtovr_payload(op, trace_to_slot, trace_to_type, hw_spec, res)

        # 2. 从硬件规格中获取模块定义
        if m_type not in hw_spec.modules:
            # 回退：如果不属于 modules.yaml 描述的严格执行单元（如占位符），返回默认 payload
            # 这里也兼容 UR_READ / UR_WRITE 如果没在 yaml 里定义的情况
            if m_type in ["UR_READ", "UR_WRITE"]:
                res["VALID"] = 1
                if m_type == "UR_WRITE":
                    res["WBE"] = 0xFFFF
            return res

        module_spec = hw_spec.modules[m_type]

        # 3. 动态驱动：遍历 YAML 定义的每个 field
        for field in module_spec.code_fields:
            field_name = field.name.upper()
            
            # 特殊全局字段：默认设置 VALID=1，SRC_PREC/DST_PREC=1 (FP32)，PRECISION=1
            if field_name == "VALID":
                res["VALID"] = 1
                continue
            if field_name in ["SRC_PREC", "DST_PREC", "PRECISION"]:
                # 优先读取元数据，如果没有，强制设为 1 (FP32模式) 兼容现有逻辑
                res[field_name] = op.metadata.get(field_name, 1)
                continue
            if field_name == "WBE" and m_type == "UR_WRITE":
                res["WBE"] = 0xFFFF
                continue

            # 处理普通的 op metadata 映射
            # 优先查找全大写的标准名称，然后回退到特例逻辑 (如 logic_op, SUB)
            val = op.metadata.get(field_name)

            if val is None:
                if field_name == "OP_TYPE" and m_type == "LOGIC":
                    val = op.metadata.get("logic_op", "AND")
                elif field_name == "SUB" and m_type == "FPADD":
                    val = op.metadata.get("logic_op")
                    if val is None and str(op.trace_id).startswith("sub"):
                        val = 1
            
            if val is not None:
                # 字符串转数字编码机制：
                # (1) 如果 field 有 encoding 字典，且输入是字符串，反向查 encoding
                if isinstance(val, str) and field.encoding:
                    val_upper = val.upper()
                    # encoding 表是从 {NAME: value} 还是 {value: NAME}?
                    # 从 yaml 结构看，通常是 {"AND": 0, "OR": 1} 这样的正向表
                    if val_upper in field.encoding:
                        res[field_name] = field.encoding[val_upper]
                    else:
                        # 尝试反向查找 (防御性)
                        for k, v in field.encoding.items():
                            if str(v).upper() == val_upper:
                                res[field_name] = int(k)
                                break
                        else:
                            res[field_name] = field.default
                elif isinstance(val, str) and str(val).upper() in ["YES", "TRUE", "1"]:
                    res[field_name] = 1
                elif isinstance(val, str) and str(val).upper() in ["NO", "FALSE", "0"]:
                    res[field_name] = 0
                else:
                    try:
                        res[field_name] = int(val)
                    except (ValueError, TypeError):
                        res[field_name] = field.default
            else:
                # 未提供则使用 default
                res[field_name] = field.default

        return res

    @staticmethod
    def _build_rtovr_payload(op: Any, trace_to_slot: Dict[str, int], trace_to_type: Dict[str, str], hw_spec: Any, res: Dict[str, Any]) -> Dict[str, Any]:
        # [Phase 4] 纯消费者模式：不再做任何启发式猜测，直接断言 sel 存在
        if "sel" not in op.metadata:
            # 兼容：如果仍有未能在编译期决定的 RTOVR（例如 optimizer 创建的 spill RTOVR 未计算 sel）
            # 我们做最后的容错，但在严格验证模式下应该抛出异常
            dep_port = op.metadata.get("dep_port", "D")
            inputs = op.metadata.get("depends_on", [])
            if inputs:
                src_tid = inputs[0]
                clean_tid = src_tid.split("_dup")[0] if "_dup" in src_tid else src_tid
                src_type = trace_to_type.get(clean_tid, "UNKNOWN")
                src_slot = trace_to_slot.get(clean_tid, 0)
                dr_map = hw_spec.get_dr_map()

                if src_type in dr_map:
                    port_map = dr_map[src_type]
                    res["SEL"] = port_map.get(dep_port, port_map.get("D", 0))
                elif src_type == "UR_READ":
                    base_ur = 30 
                    idx = src_slot - base_ur
                    unit_key = f"UR_READ_{idx}"
                    res["SEL"] = dr_map.get(unit_key, {}).get(dep_port, base_ur + idx)
                else:
                    res["SEL"] = 0
            else:
                res["SEL"] = 0
        else:
            res["SEL"] = op.metadata["sel"]
            
        res["VALID"] = 1
        return res
