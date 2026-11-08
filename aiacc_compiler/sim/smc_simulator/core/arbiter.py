from typing import Dict, List, Set, Any
import logging
from collections import defaultdict

class ResourceArbiter:
    """
    增强后的资源仲裁器 (Resource Arbiter)
    负责检测和处理硬件资源冲突：
    1. Bank 读写端口冲突 (Per-Bank Port Limit)
    2. 最大并行 IQ 数量冲突 (Parallelism Limit)
    3. 同周期写冲突 (Write-back collision, 预留)
    
    [Phase 6] 增加资源冲突统计功能
    """
    def __init__(self, system_spec: Any):
        self.spec = system_spec
        self.logger = logging.getLogger("Arbiter")
        
        # 从规格中读取限制
        res = system_spec.get('resource_constraints', {})
        self.max_read_ports = res.get('read_ports_per_bank', 1)
        self.max_write_ports = res.get('write_ports_per_bank', 1)
        self.max_parallel_iq = res.get('max_parallel_iq', 10)
        
        # 当前周期的资源映射
        self.current_reads = {}   # bank_id -> count
        self.current_writes = {}  # bank_id -> count
        self.current_read_conflicts = defaultdict(int)
        self.current_write_conflicts = defaultdict(int)
        self.active_iq_count = 0

        # [Phase 6] 统计信息
        self.stats = {
            'total_cycles': 0,
            'iq_active_total': 0,
            'iq_active_max': 0,
            'parallel_iq_conflicts': 0,
            'bank_read_access': defaultdict(int),   # bank_id -> attempts
            'bank_read_conflicts': defaultdict(int), # bank_id -> failures
            'bank_write_access': defaultdict(int),  # bank_id -> attempts
            'bank_write_conflicts': defaultdict(int),# bank_id -> failures
        }

    def reset_cycle(self):
        """每拍开始前重置计数"""
        self.current_reads = {}
        self.current_writes = {}
        self.current_read_conflicts = defaultdict(int)
        self.current_write_conflicts = defaultdict(int)
        self.active_iq_count = 0

    def request_read(self, bank_id: int, requester_name: str = "Unknown") -> bool:
        """投机性请求读端口"""
        count = self.current_reads.get(bank_id, 0)
        
        # 统计访问尝试
        self.stats['bank_read_access'][bank_id] += 1
        
        if count >= self.max_read_ports:
            # 统计冲突
            self.stats['bank_read_conflicts'][bank_id] += 1
            self.current_read_conflicts[bank_id] += 1
            self.logger.error(f"Bank {bank_id} Read Conflict: {requester_name} requested, but ports full ({self.max_read_ports})")
            return False
        
        self.current_reads[bank_id] = count + 1
        return True

    def request_write(self, bank_id: int, requester_name: str = "Unknown") -> bool:
        """投机性请求写端口"""
        count = self.current_writes.get(bank_id, 0)
        
        # 统计访问尝试
        self.stats['bank_write_access'][bank_id] += 1
        
        if count >= self.max_write_ports:
            # 统计冲突
            self.stats['bank_write_conflicts'][bank_id] += 1
            self.current_write_conflicts[bank_id] += 1
            self.logger.error(f"Bank {bank_id} Write Conflict: {requester_name} requested, but ports full ({self.max_write_ports})")
            return False
        
        self.current_writes[bank_id] = count + 1
        return True

    def check_conflicts(self, ur_inputs: Dict[str, Any], active_iqs: List[str]):
        """
        全面检测一拍中的所有输入是否存在冲突。
        """
        self.reset_cycle()
        errors = []
        
        self.stats['total_cycles'] += 1
        
        # 1. 检查并行 IQ 限制 (按用户要求取消限制)
        self.active_iq_count = len(active_iqs)
        self.stats['iq_active_total'] += self.active_iq_count
        if self.active_iq_count > self.stats['iq_active_max']:
            self.stats['iq_active_max'] = self.active_iq_count
        
        # 2. 检查 UR 读请求冲突
        for key, req in ur_inputs.items():
            if key.startswith('R') and req.get('en'):
                bank = req.get('bank')
                if not self.request_read(bank, key):
                    errors.append(f"Read conflict on Bank {bank} from {key}")
                    
        # 3. 检查 UR 写请求冲突
        for key, req in ur_inputs.items():
            if key.startswith('W') and req.get('en'):
                bank = req.get('bank')
                if not self.request_write(bank, key):
                    errors.append(f"Write conflict on Bank {bank} from {key}")
        
        if errors:
            raise ResourceConflictError(errors[0])
        
        return True

    def get_stats(self) -> Dict[str, Any]:
        """获取当前统计信息"""
        # 计算平均并行度
        avg_iq = 0
        if self.stats['total_cycles'] > 0:
            avg_iq = self.stats['iq_active_total'] / self.stats['total_cycles']
            
        return {
            'total_cycles': self.stats['total_cycles'],
            'iq_stats': {
                'avg_active': avg_iq,
                'max_active': self.stats['iq_active_max'],
                'conflicts': self.stats['parallel_iq_conflicts']
            },
            'bank_stats': {
                'read_access': dict(self.stats['bank_read_access']),
                'read_conflicts': dict(self.stats['bank_read_conflicts']),
                'write_access': dict(self.stats['bank_write_access']),
                'write_conflicts': dict(self.stats['bank_write_conflicts'])
            }
        }

class ResourceConflictError(Exception):
    """硬件资源冲突异常"""
    pass
