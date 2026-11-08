from typing import Dict, Any, List, Set, Tuple
from smc_simulator.core.base import Component

class RtoVRUnit(Component):
    """
    RtoVR Interconnect Unit。
    负责在输入源（Units DR, UR Read）和输出端口（Units DVR, UR Write）之间进行数据路由。
    
    特性：
    - Pulse Behavior: 路由指令当拍有效。
    - Latency=1: 数据传输经过一拍延迟。
    """
    def __init__(self, name: str, num_outputs: int = 40):
        super().__init__(name)
        self.num_outputs = num_outputs
        self.outputs = [0] * num_outputs
        self.valid_outputs = set()
        self._next_outputs = [0] * num_outputs
        self._next_valid = set()

    def reset(self):
        self.outputs = [0] * self.num_outputs
        self.valid_outputs = set()
        self._next_outputs = [0] * self.num_outputs
        self._next_valid = set()

    def evaluate(self, inputs: Dict[str, Any]):
        sources = inputs.get('sources', {})
        iq_updates = inputs.get('iq_updates', [])
        cycle = inputs.get('cycle', -1)
        
        self._next_outputs = [0] * self.num_outputs
        self._next_valid = set()

        for port_id, inst in iq_updates:
            payload_valid = inst & 1
            if payload_valid:
                 src_id = (inst >> 1) & 0x3F
                 if 0 <= port_id < self.num_outputs:
                      val = sources.get(src_id, 0)
                      print(f">>> [AUDIT-DATA] Cyc:{cycle} Unit:RTOVR Tag:ROUTE Port:{port_id} Src:{src_id} V0:{val:032x}")
                      self._next_outputs[port_id] = val
                      self._next_valid.add(port_id)

    def tick(self, cycle: int = -1):
        self.outputs = self._next_outputs
        self.valid_outputs = self._next_valid
        
        self._next_outputs = [0] * self.num_outputs
        self._next_valid = set()

    def get_output(self, port_id: int) -> int:
        if 0 <= port_id < self.num_outputs:
            return self.outputs[port_id]
        return 0

    def is_valid(self, port_id: int) -> bool:
        return port_id in self.valid_outputs

    def get_regs(self) -> Dict[str, int]:
        """Debug helper: returns values of all non-zero active output clusters."""
        regs = {}
        for i, val in enumerate(self.outputs):
            if val != 0:
                regs[f'Port_{i}'] = val
        return regs
