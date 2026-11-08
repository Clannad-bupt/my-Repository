# src/tools/pipeline_utils.py
import os
from pathlib import Path
import json

def setup_isolated_build_dir(base_root, op_category, op_name, layout_id=None):
    """
    🛸 [ADPC V4.0] 物理仿真路径隔离工具
    """
    path = Path(base_root) / op_category / op_name
    if layout_id is not None:
        path = path / f"layout_{layout_id}"
    path.mkdir(parents=True, exist_ok=True)
    return path

def apply_bank_overrides(dfg, bank_map):
    """
    👽 [ADPC V4.0] 物理劫持器：在 SMT 综合前，将 DFG 中的逻辑节点强行绑定到物理 Bank。
    """
    for node in dfg:
        # 🛸 隐藏 Bug 修复：处理 MIROp 对象和原始 Dict 的兼容性
        metadata = node.metadata if hasattr(node, "metadata") else node.get("metadata", {})
        actor_name = metadata.get("meta_name") or metadata.get("role")
        
        if actor_name in bank_map:
            metadata["manual_bank"] = bank_map[actor_name]
        elif "constants" in bank_map and "cinl" in str(node if not hasattr(node, "trace_id") else node.trace_id):
            # 将所有常量重定向到指定的常量池 Bank
            metadata["manual_bank"] = bank_map["constants"]
    return dfg

def save_layout_metadata(build_path, bank_map):
    with open(Path(build_path) / "layout_config.json", "w") as f:
        json.dump(bank_map, f, indent=4)
