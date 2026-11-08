import os
from typing import Dict, List, Any
from smc_simulator.core.base import Component

class URBank(Component):
    """
    重构后的 Unified Register (UR) Bank。
    支持从 System Spec 动态配置 Bank 数量和读写端口数量。
    """
    def __init__(self, name: str, num_banks: int = 8, bank_size: int = 256, num_read_ports: int = 4):
        super().__init__(name)
        self.num_banks = num_banks
        self.bank_size = bank_size
        self.num_read_ports = num_read_ports
        
        # 存储：storage[bank_id][addr]
        self.storage = [[0] * bank_size for _ in range(num_banks)]
        
        # 读端口输出锁存 (Latency=1)
        self.read_ports_out = [0] * num_read_ports
        self._next_read_ports_out = [0] * num_read_ports
        
        self._pending_writes = {} # (bank, addr) -> data

    def reset(self):
        self.storage = [[0] * self.bank_size for _ in range(self.num_banks)]
        self.read_ports_out = [0] * self.num_read_ports
        self._next_read_ports_out = [0] * self.num_read_ports
        self._pending_writes = {}

    def load_hex(self, bank_id: int, file_path: str):
        if not (0 <= bank_id < self.num_banks) or not os.path.exists(file_path):
            return
        with open(file_path, 'r') as f:
            addr = 0
            for line in f:
                if addr >= self.bank_size: break
                line = line.strip()
                if not line or line.startswith('#'): continue
                self.storage[bank_id][addr] = int(line, 16)
                addr += 1

    def dump_hex(self, bank_id: int, file_path: str):
        """将指定 Bank 的数据导出为 HEX 文件（128-bit 每行）。"""
        if not (0 <= bank_id < self.num_banks):
            return
        with open(file_path, 'w') as f:
            for val in self.storage[bank_id]:
                # 统一 128-bit (32 hex chars) 格式
                f.write(f"0x{val:032x}\n")

    def evaluate(self, inputs: Dict[str, Any], cycle: int = -1):
        """
        inputs: {'R0': {...}, 'W0': {...}}
        """
        # 物理对齐：UR 读端口在硬件中通常带有输出数据锁存器（Output Data Register）。
        # 数据会一直保持，直到下一次有效读取覆盖它。
        self._next_read_ports_out = list(self.read_ports_out)
        
        # 读逻辑
        for i in range(self.num_read_ports):
            req = inputs.get(f'R{i}')
            if req and req.get('en'):
                b, a = req['bank'], req['addr']
                if 0 <= b < self.num_banks and 0 <= a < self.bank_size:
                    val = self.storage[b][a]
                    self._next_read_ports_out[i] = val
                    print(f">>> [AUDIT-DATA] Cyc:{cycle} Unit:UR-READ Tag:READ Port:{i} Bank:{b} Addr:{a} V0:{val:032x}")


        # 写逻辑
        self._pending_writes = {}
        for i in range(self.num_read_ports): # 假设写端口数量与读端口一致
            req = inputs.get(f'W{i}')
            if req and req.get('en'):
                b, a, d = req['bank'], req['addr'], req.get('data', 0)
                if 0 <= b < self.num_banks and 0 <= a < self.bank_size:
                    self._pending_writes[(b, a)] = d
                    print(f">>> [AUDIT-DATA] Cyc:{cycle} Unit:UR-WRITE Tag:WRITE Port:{i} Bank:{b} Addr:{a} V0:{d:032x}")

    def tick(self, cycle: int = -1):
        self.read_ports_out = self._next_read_ports_out
        for (b, a), d in self._pending_writes.items():
            self.storage[b][a] = d

    def get_read_data(self, port_idx: int) -> int:
        return self.read_ports_out[port_idx] if 0 <= port_idx < self.num_read_ports else 0
