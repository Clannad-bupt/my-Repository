"""
sim/tests/helpers.py
====================
测试共享工具：MockSpec、MIROp 工厂。
所有 Group 均从此处导入，避免重复代码。
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'src'))

from ir.mir.mir_op import MIROp


class MockSpec:
    """
    轻量 HardwareSpec Mock。
    仅提供 IIAnalyzer / SynthesisAxioms 所需的最小接口。
    """
    def __init__(self, instances: dict, delays: dict,
                 read_ports: int = 4, write_ports: int = 4,
                 iq_registry: dict = None):
        self.delays = delays
        self._instances = instances
        self._read_ports = read_ports
        self._write_ports = write_ports
        self._registry = iq_registry or {"UR_READ": 30, "UR_WRITE": 35}
        # 自动为 Mock 的单元分配基准 ID (100, 200, 300...)
        if iq_registry is None:
            for i, u_type in enumerate(instances.keys()):
                if u_type not in self._registry:
                    self._registry[u_type] = (i + 1) * 100
        self.config = {
            "iq_registry": self._registry,
            "resource_constraints": {
                "unit_instances": instances,
                "total_read_ports": read_ports,
                "total_write_ports": write_ports,
            },
        }

    def get_iq_ids(self, unit_type: str):
        key = unit_type.upper()
        registry = self._registry
        instances = self._instances
        if key == "UR_READ":
            base = registry.get("UR_READ", 30)
            return [base + i for i in range(self._read_ports)]
        if key == "UR_WRITE":
            base = registry.get("UR_WRITE", 35)
            return [base + i for i in range(self._write_ports)]
        if key in instances:
            base = registry.get(key, 0)
            return [base + i for i in range(instances[key])]
        return []

    def get_iq_name(self, iq_id: int) -> str:
        """Mock 版反向寻址"""
        registry = self._registry
        base_r = registry.get("UR_READ", 30)
        base_w = registry.get("UR_WRITE", 35)
        if base_r <= iq_id < base_r + self._read_ports: return f"ur-r{iq_id - base_r}"
        if base_w <= iq_id < base_w + self._write_ports: return f"ur-w{iq_id - base_w}"
        
        for u_type, base_id in registry.items():
            count = self._instances.get(u_type, 0)
            if base_id <= iq_id < base_id + count:
                idx = iq_id - base_id
                return f"{u_type.lower()}-{idx}"
        return f"unit-{iq_id}"


def make_op(op_type: str, trace_id: str,
            bank_id=None, deps=None, iq_id=None, **kwargs) -> MIROp:
    """
    MIROp 工厂 (V2.0)。
    支持通过 **kwargs 注入任意 metadata 属性。
    """
    meta = {}
    if deps:
        meta["depends_on"] = deps
    if bank_id is not None:
        meta["manual_bank"] = bank_id
    if iq_id is not None:
        meta["iq_id"] = iq_id
    
    # 🛸 [NEW] 注入所有自定义属性
    for k, v in kwargs.items():
        meta[k] = v
        
    return MIROp(op_type=op_type, logic_cycle=0,
                 trace_id=trace_id, metadata=meta)
