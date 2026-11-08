import pytest
from orchestrator.colorer import BalancedBankColorer

class MockTensor:
    def __init__(self, name, start_t, end_t, manual_bank=None):
        self.name = name
        self.start_t = start_t
        self.end_t = end_t
        self.manual_bank = manual_bank
        
    def __repr__(self):
        return f"T[{self.name}]({self.start_t}-{self.end_t})"

def test_bank_basic_coloring():
    """测试基本的无冲突着色"""
    tensors = [
        MockTensor("A", 0, 10),
        MockTensor("B", 0, 10),
        MockTensor("C", 11, 20) 
    ]
    conflicts = [("A", "B")]
    coloring, success = BalancedBankColorer.assign_banks(tensors, conflicts)
    
    assert success is True
    assert coloring["A"] != coloring["B"]
    assert coloring["C"] < 8

def test_bank_manual_pinning():
    """测试强锁定功能"""
    tensors = [
        MockTensor("A", 0, 10, manual_bank=7), 
        MockTensor("B", 0, 10)
    ]
    conflicts = [("A", "B")]
    coloring, success = BalancedBankColorer.assign_banks(tensors, conflicts)
    
    assert coloring["A"] == 7
    assert coloring["B"] != 7

def test_bank_exhaustion_report():
    """测试 Bank 耗尽报告 (10个冲突张量抢 8个 Bank)"""
    tensors = [MockTensor(str(i), 0, 10) for i in range(10)]
    conflicts = []
    for i in range(10):
        for j in range(i + 1, 10):
            conflicts.append((str(i), str(j)))
            
    coloring, success = BalancedBankColorer.assign_banks(tensors, conflicts, bank_num=8)
    
    assert success is False
    # 确认超支张量被分配了 > 7 的虚拟编号，准备交予重调度
    assert any(b >= 8 for b in coloring.values())
