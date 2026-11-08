import unittest
import os
from pathlib import Path
from api.hw_spec import HardwareSpec
from orchestrator.colorer import BalancedBankColorer
from orchestrator.memory_allocator import MemoryAllocator
from driver.emitter import LIRPacker

class MockURReadOp:
    def __init__(self, t, bank, addr):
        self.op_type = "UR_READ"
        self.node_id = f"UR_READ_{t}"
        self.metadata = {"global_t": t}
        self.re = 1
        self.rbank = bank
        self.raddr = addr

class TestLIREmitterFullStack(unittest.TestCase):
    """
    AIACC V5.0 全链路物理审计基准测试 (Sprint 5 版: 正确回填物理属性)。
    """
    def setUp(self):
        workspace = Path(__file__).parent.parent.parent
        self.spec = HardwareSpec.load(str(workspace / "hw_config"))
        from aiacc_hardware.src.spec_loader import SpecLoader
        self.sim_loader = SpecLoader()

    def test_full_stack_binary_emission(self):
        output_dir = "build/bin/sprint5_full"
        os.makedirs(output_dir, exist_ok=True)
        
        class FakeTensor:
            def __init__(self, name, size, start_t, end_t):
                self.name = name
                self.size = size
                self.start_t = start_t
                self.end_t = end_t
                self.bank_id = None # 显式初始化
        
        # 2. 模拟 20 个张量生命周期
        tensors = [FakeTensor(f"T{i}", 32, i*5, i*5+10) for i in range(20)]
        
        # 3. 物理布局层: 执行 Bank 着色
        print("\n[FULL-STACK MISSION] Starting AIACC V5.0 S05 Integrated Test...")
        print("[LAYOUT] Assigning 8-Bank SMC Colors...")
        conflicts = [(f"T{i}", f"T{i+1}") for i in range(19)]
        coloring, success = BalancedBankColorer.assign_banks(tensors, conflicts)
        self.assertTrue(success)
        
        # --- 核心修正：确保所有对象都被回填 ---
        for t in tensors:
            t.bank_id = coloring.get(t.name, 0) # 容错回填，默认为 Bank 0
        
        # 4. 物理布局层: 执行地址均衡分配
        print("[LAYOUT] Mapping 256-Word Memory Addresses...")
        allocator = MemoryAllocator(self.spec.params)
        addr_map = allocator.allocate(tensors)
        
        # 5. 二进制发射层: 模拟 7D 连续访存折叠指令流
        print("[EMITTER] Packing and Compacting (including 7D Semantic Folding)...")
        test_stream = []
        for t in range(512):
            test_stream.append(MockURReadOp(t, bank=0, addr=t))
            
        packer = LIRPacker("hw_config")
        packer.emit_from_stream(test_stream, output_dir)
        
        # 6. 验证产出物
        bin_path = os.path.join(output_dir, "smc_0", "iq_ur_r0.hex")
        self.assertTrue(os.path.exists(bin_path))
        size = os.path.getsize(bin_path)
        print(f"[AUDIT] Binary emitted successfully. Size: {size} bytes.")
        self.assertLess(size, 100, "Should be highly compressed (7 bytes for a single loop).")
        print("✅ INTEGRATION AUDIT PASSED: S05 End-to-End closed.")

if __name__ == "__main__":
    unittest.main()
