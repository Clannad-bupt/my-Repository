import json
import os
from pathlib import Path
from typing import Dict, Optional

class RegressionManager:
    """
    AIACC 稳定性管理：负责存储与核验算子的物理时序快照。
    确保编译器优化不会破坏存量算子的物理正确性。
    """
    def __init__(self, snapshot_dir: str = "src/library/atomic/regressions"):
        self.snapshot_dir = Path(snapshot_dir)
        self.snapshot_dir.mkdir(parents=True, exist_ok=True)

    def get_snapshot_path(self, operator_name: str, mode: str) -> Path:
        return self.snapshot_dir / f"{operator_name}_{mode}_golden.json"

    def save_golden(self, operator_name: str, mode: str, result: Dict[str, int]):
        """保存当前时序为黄金基准"""
        path = self.get_snapshot_path(operator_name, mode)
        with open(path, "w") as f:
            json.dump(result, f, indent=2)
        print(f"✅ Saved Golden Snapshot for [{operator_name}] in [{mode}] mode.")

    def verify(self, operator_name: str, mode: str, current_result: Dict[str, int]) -> bool:
        """核验当前时序是否与黄金基准冲突（或关键指标退化）"""
        path = self.get_snapshot_path(operator_name, mode)
        if not path.exists():
            print(f"⚠️ No golden snapshot for [{operator_name}] in [{mode}] mode. Skipping verification.")
            return True
            
        with open(path, "r") as f:
            golden = json.load(f)
            
        # 核心稳定性检查：
        # 1. 节点数量必须一致 (确保 DFG 没被意外修改)
        if len(golden) != len(current_result):
            print(f"❌ Stability Failed: Node count mismatch for [{operator_name}].")
            return False
            
        # 2. 时序长度检查 (如果不允许任何性能退化)
        max_golden = max(golden.values())
        max_current = max(current_result.values())
        if max_current > max_golden:
            print(f"❌ Stability Failed: Performance REGRESSION for [{operator_name}]. Golden: {max_golden}, Current: {max_current}")
            return False
            
        return True
