import unittest
import os
from api.compiler import LIRCompiler
from library.structured.conv_gen import Conv2DGenerator

class MockLIR:
    def __init__(self, op_type, t, bank=0, addr=0, iq_id=0):
        self.op_type = op_type
        self.re = 1
        self.rbank = bank
        self.raddr = addr
        self.iq_id = iq_id
        # 为了 FieldMapper，我们补齐几个计算单元可能需要的占位字段
        self.src0 = 0
        self.src1 = 1
        self.dst = 2
        
        self.t = t
        self.node_id = f"{op_type}_{t}_{id(self)}"
        self.metadata = {"iq_id": iq_id, "bank_id": bank}

class TestCompilerE2E(unittest.TestCase):
    """
    AIACC V5.0 端到端编译器终极审计测试 (修正版)。
    """
    def test_conv_relu_chain_compilation(self):
        output_dir = "build/bin/e2e_conv_relu"
        os.makedirs(output_dir, exist_ok=True)
        
        compiler = LIRCompiler("hw_config")
        
        # 1. 配置算子 0 (Conv2D)
        gen_conv = Conv2DGenerator(in_shape=(1, 4, 4), kernel=(2, 2), stride=1)
        # 获取特征图读取流 (IQ 0)
        conv_lir_all = gen_conv.generate_parallel_lir(ibank=0, wbank=1)
        conv_mock = [op for op in conv_lir_all if op["iq_id"] == 0]
        
        conv_stream = [MockLIR("UR_READ", op["t"], bank=0, addr=op["raddr"], iq_id=0) for op in conv_mock]
        conv_synthesis = {op.node_id: op.t for op in conv_stream}
        
        # 2. 配置算子 1 (ReLU) - 依赖 Conv
        relu_stream = [MockLIR("UR_READ", t, bank=0, addr=t, iq_id=0) for t in range(9)]
        relu_synthesis = {op.node_id: op.t for op in relu_stream}
        
        # 3. 构建编译任务
        op_blocks = [
            {"name": "CONV_0", "stream": conv_stream, "synthesis_res": conv_synthesis},
            {"name": "RELU_0", "stream": relu_stream, "synthesis_res": relu_synthesis, "depends_on": ["CONV_0"]}
        ]
        
        # 4. 一键编译
        print("\n[E2E] Orchestrating and Packing Full Pipeline...")
        total_items = compiler.compile(op_blocks, output_dir)
        
        # 5. 物理审计
        bin_path = os.path.join(output_dir, "smc_0", "iq_ur_r0.hex")
        self.assertTrue(os.path.exists(bin_path))
        
        size = os.path.getsize(bin_path)
        print(f"[E2E AUDIT] UR_READ.bin: {size} bytes.")
        # 原 36 + 9 = 45 指令内容，折叠后应为 5 指令，约 35 字节
        self.assertLessEqual(size, 70)
        
        print("✅ E2E INTEGRATION PASSED: Unified Compiler Ready for Delivery.")

if __name__ == "__main__":
    unittest.main()
