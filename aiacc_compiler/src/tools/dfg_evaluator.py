"""
AIACC DFG 数学求值引擎 (SSoT - Single Source of Truth)

所有需要「从 DFG 顺推数学值」的场景统一调用此模块:
  - standard_pipeline_runner.py (Stage A 验证)
  - pipeline_verifier.py (独立验证)
  - cycle_verifier.py (后验对比)
  - debug_pipeline.py (一键定位)
"""
import json
import struct
import numpy as np
from typing import Dict, Any, List, Optional


def parse_deps(meta: dict) -> list:
    """统一解析 depends_on 字段为 list。
    
    兼容两种格式:
      - Python list (编译时内存对象)
      - JSON string "['a', 'b']" (持久化后的 JSON 文件)
    """
    raw = meta.get("depends_on", [])
    if isinstance(raw, list):
        return raw
    if isinstance(raw, str):
        try:
            return json.loads(raw.replace("'", '"'))
        except Exception:
            return [raw] if raw else []
    return []


def eval_dfg(dfg_nodes, input_data: Dict[str, Any]) -> Dict[str, float]:
    """
    从 DFG 顺推每个节点的数学期望值。
    
    Args:
        dfg_nodes: DFG 节点列表。支持两种格式:
            - 编译时对象 (有 .trace_id, .op_type, .metadata 属性)
            - JSON 反序列化后的 dict 列表
        input_data: 输入数据字典, e.g. {"x": np.array([...]) 或 float}
    
    Returns:
        val_map: {node_trace_id: float_value}
    """
    val_map = {}

    # 统一接口: 支持编译时对象和 JSON dict
    def _get(node, key, default=None):
        if isinstance(node, dict):
            return node.get(key, default)
        return getattr(node, key, default)

    def _get_meta(node):
        if isinstance(node, dict):
            return node.get("metadata", {})
        return getattr(node, "metadata", {})

    nodes_by_id = {}
    for n in dfg_nodes:
        tid = _get(n, "trace_id")
        nodes_by_id[tid] = n

    def eval_op(nid: str) -> float:
        if nid in val_map:
            return val_map[nid]
        if nid not in nodes_by_id:
            return 0.0

        node = nodes_by_id[nid]
        op_type = _get(node, "op_type", "")
        meta = _get_meta(node)
        deps = parse_deps(meta)

        # ─── UR_READ: 常量或变量读取 ───
        if op_type == "UR_READ":
            is_const = str(meta.get("is_const", "")).lower() == "true"
            is_int = str(meta.get("is_int", "False")).lower() == "true"
            if is_const or nid.startswith("cinl_"):
                v_str = meta.get("value", None)
                if v_str is not None:
                    val = int(v_str) if is_int else float(v_str)
                else:
                    val = 0.0
                val_map[nid] = val
                return val
            else:
                m_name = meta.get("meta_name", nid)
                # 优先从 spill 回读
                spill_id = f"spill_{m_name}"
                if spill_id in val_map:
                    val = val_map[spill_id]
                else:
                    d = input_data.get(m_name, 0.0)
                    if isinstance(d, (list, np.ndarray)):
                        val = float(np.asanyarray(d).ravel()[0])
                    else:
                        val = float(d)
                val_map[nid] = val
                return val

        # ─── RTOVR: 透传路由 ───
        if op_type == "RTOVR":
            target_port = str(meta.get("dep_port", "D")).upper()
            base_val = eval_op(deps[0]) if deps else 0.0
            if target_port == "ST":
                # 🛸 仿真数学建模: 将计算结果映射为状态位掩码 (SSoT)。
                # FPADD 在 (S0 - S1) 分支下硬件产出位图为: GT=bit2(4), EQ=bit1(2), LS=bit0(1)
                # 注: 这里 base_val 就是 v0 - v1 的数学值
                st_val = 4.0 if base_val > 1e-10 else 1.0 if base_val < -1e-10 else 2.0
                val_map[nid] = st_val
                return st_val
            else:
                val_map[nid] = base_val
                return base_val

        # ─── FPADD: 浮点加法 ───
        if op_type == "FPADD":
            v0 = eval_op(deps[0]) if len(deps) > 0 else 0.0
            v1 = eval_op(deps[1]) if len(deps) > 1 else 0.0
            is_sub = str(meta.get("SUB", "0")) in ("1", "SUB")
            val = v0 - v1 if is_sub else v0 + v1
            val_map[nid] = val
            return val

        # ─── FPMUL: 浮点乘法 ───
        if op_type == "FPMUL":
            v0 = eval_op(deps[0]) if len(deps) > 0 else 0.0
            v1 = eval_op(deps[1]) if len(deps) > 1 else 0.0
            val = v0 * v1
            val_map[nid] = val
            return val

        # ─── Max/Min (阶段 A 逻辑节点) ───
        if op_type == "Max":
            v0 = eval_op(deps[0]) if len(deps) > 0 else 0.0
            v1 = eval_op(deps[1]) if len(deps) > 1 else 0.0
            val = max(v0, v1)
            val_map[nid] = val
            return val
        
        if op_type.upper() == "MIN":
            v0 = eval_op(deps[0]) if len(deps) > 0 else 0.0
            v1 = eval_op(deps[1]) if len(deps) > 1 else 0.0
            val = min(v0, v1)
            val_map[nid] = val
            return val

        if op_type.upper() in ["TRUNC", "FPTOINT"]:
            v0 = eval_op(deps[0]) if deps else 0.0
            val = float(int(v0))
            val_map[nid] = val
            return val

        if op_type.upper() in ["FLOOR"]:
            v0 = eval_op(deps[0]) if deps else 0.0
            val = float(np.floor(v0))
            val_map[nid] = val
            return val

        if op_type.upper() in ["CEILING", "CEIL"]:
            v0 = eval_op(deps[0]) if deps else 0.0
            val = float(np.ceil(v0))
            val_map[nid] = val
            return val

        if op_type.upper() == "INTTOFP":
            # 硬件 INTTOFP 仅做透传（配合 FPTOINT 实现截断）
            val = eval_op(deps[0]) if deps else 0.0
            val_map[nid] = val
            return val

        # ─── LOGIC: 逻辑与选择电路 ───
        if op_type == "LOGIC":
            v0 = eval_op(deps[0]) if len(deps) > 0 else 0.0
            v1 = eval_op(deps[1]) if len(deps) > 1 else 0.0
            st = eval_op(deps[2]) if len(deps) > 2 else 0.0
            op_str = str(meta.get("OP_TYPE", "AND")).upper()
            
            def to_bits(f):
                if isinstance(f, int): return f & 0xFFFFFFFF
                return struct.unpack('>I', struct.pack('>f', float(f)))[0]

            def to_float(i):
                return struct.unpack('>f', struct.pack('>I', int(i) & 0xFFFFFFFF))[0]

            if "SELECT_GT" in op_str:
                 val = v0 if int(st) & 0x4 else v1
            elif "SELECT_GE" in op_str:
                 val = v0 if int(st) & 0x6 else v1
            elif "SELECT_LE" in op_str:
                 val = v0 if int(st) & 0x3 else v1
            elif "SELECT_EQ" in op_str:
                 val = v0 if int(st) & 0x2 else v1
            elif "SELECT_LS" in op_str:
                 val = v0 if int(st) & 0x1 else v1
            elif "SELECT_NE" in op_str:
                 val = v0 if not (int(st) & 0x2) else v1
            elif "AND" in op_str:
                 val = to_float(to_bits(v0) & to_bits(v1))
            elif "OR" in op_str:
                 val = to_float(to_bits(v0) | to_bits(v1))
            elif "XOR" in op_str:
                 val = to_float(to_bits(v0) ^ to_bits(v1))
            elif "NOT" in op_str:
                 val = to_float(~to_bits(v0))
            else:
                 val = v0 # Fallback
            val_map[nid] = val
            return val

        # ─── UR_WRITE: 内存写入 ───
        if op_type.upper() == "UR_WRITE":
            val = eval_op(deps[0]) if deps else 0.0
            val_map[nid] = val
            return val

        val_map[nid] = 0.0
        return 0.0

    for n in dfg_nodes:
        eval_op(_get(n, "trace_id"))
    return val_map


def get_output_value(dfg_nodes, val_map: Dict[str, float], out_name: str = "y") -> Optional[float]:
    """获取最终输出节点的值。"""
    for n in dfg_nodes:
        if isinstance(n, dict):
            if n.get("op_type") == "UR_WRITE" and n.get("metadata", {}).get("meta_name") == out_name:
                return val_map.get(n["trace_id"])
        else:
            if getattr(n, "op_type", "") == "UR_WRITE" and n.metadata.get("meta_name") == out_name:
                return val_map.get(n.trace_id)
    return None


def verify_dfg_topology(dfg_nodes) -> List[str]:
    """检查 DFG 拓扑连通性: 所有依赖是否存在。"""
    errors = []
    node_ids = set()
    for n in dfg_nodes:
        tid = n["trace_id"] if isinstance(n, dict) else n.trace_id
        node_ids.add(tid)

    for n in dfg_nodes:
        meta = n.get("metadata", {}) if isinstance(n, dict) else getattr(n, "metadata", {})
        tid = n["trace_id"] if isinstance(n, dict) else n.trace_id
        for dep in parse_deps(meta):
            if dep not in node_ids:
                errors.append(f"Node {tid} depends on missing node {dep}")
    return errors
