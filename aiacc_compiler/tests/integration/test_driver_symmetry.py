import unittest
import sys
import os
from pathlib import Path

# 保证导入路径
sys.path.append(str(Path(__file__).parent.parent.parent / "src"))
sys.path.append(str(Path(__file__).parent.parent.parent / "sim"))

from api.hw_spec import HardwareSpec
from driver.field_mapper import FieldMapper
from driver.packer import BitPacker
from smc_simulator.core.decoder import InstructionDecoder
from ir.mir.mir_op import MIROp

class TestDriverSymmetry(unittest.TestCase):
    """
    硬件对称性测试：验证 Driver 生成的指令与 Simulator 解码的位平面完全一致。
    覆盖：UR_READ, UR_WRITE, RTOVR, FPADD, FPMUL
    """
    
    def setUp(self):
        workspace = Path(__file__).parent.parent.parent
        self.spec = HardwareSpec.load(str(workspace / "hw_config"))
        from aiacc_hardware.src.spec_loader import SpecLoader
        self.sim_loader = SpecLoader()

    def check_unit_symmetry(self, unit_name, test_meta):
        print(f"\n[SYMMETRY-CHECK] Testing Unit: {unit_name}")
        
        # 1. 构造 MIR 算子
        op = MIROp(unit_name, 0, f"test_{unit_name}", metadata=test_meta)
        module_spec = self.spec.modules.get(unit_name)
        self.assertIsNotNone(module_spec, f"Module spec for {unit_name} not found")
        
        # 2. Driver Layer: Map to Payload & Pack
        iq_name = f"IQ_{unit_name}"
        if unit_name == "RTOVR": iq_name = "IQ_RTOVR"
        iq_conf = self.spec.iqs[iq_name]
        
        payload = FieldMapper.map_op_to_payload(op, module_spec)
        # 填充 Header (VLD=1, 其他默认)
        header_val = BitPacker.pack_capsule(
            iq_name=iq_name, iq_conf=iq_conf, payload=payload,
            loops=test_meta.get("loops", 0),
            inc=test_meta.get("inc", 0),
            dly=test_meta.get("dly", 0)
        )
        
        print(f"  > Driver Packed Hex: {header_val:032x}")
        
        # 3. Simulator Layer: Decode
        sim_unit_spec = self.sim_loader.get_unit(unit_name)
        decoder = InstructionDecoder(sim_unit_spec)
        decoded = decoder.decode(header_val)
        
        print(f"  > Simulator Decoded: {decoded.to_dict()}")
        
        # 4. 位平面对齐校验
        # 校验 Header 字段
        self.assertEqual(decoded.VLD, 1, "VLD mismatch")
        self.assertEqual(decoded.LOOPS, test_meta.get("loops", 0), "LOOPS mismatch")
        self.assertEqual(decoded.INC, test_meta.get("inc", 0), "INC mismatch")
        self.assertEqual(decoded.EMBED_LOOPS, test_meta.get("embed", 0), "EMBED_LOOPS mismatch")
        self.assertEqual(decoded.DLY, test_meta.get("dly", 0), "DLY mismatch")
        
        # 校验 Micro-Op 业务字段
        # 注意：Decoder 可能会根据 spec 将整数映射为枚举字符串，我们需要灵活比对
        for field in sim_unit_spec.micro_instructions:
            fname = field.name
            if fname in ["re", "we", "vld"]:
                self.assertEqual(decoded[fname], 1, f"Control bit {fname} mismatch")
            elif fname in ["bank_id", "rbank", "wbank"]:
                self.assertEqual(decoded[fname], test_meta.get("bank_id"), f"Bank ID {fname} mismatch")
            elif fname in ["p_addr", "raddr", "waddr"]:
                self.assertEqual(decoded[fname], test_meta.get("p_addr"), f"Addr {fname} mismatch")

    def test_ur_read_symmetry(self):
        meta = {"bank_id": 2, "p_addr": 128, "loops": 5, "inc": 1}
        self.check_unit_symmetry("UR_READ", meta)

    def test_ur_write_symmetry(self):
        meta = {"bank_id": 7, "p_addr": 64, "loops": 0}
        self.check_unit_symmetry("UR_WRITE", meta)

    def test_fpadd_symmetry(self):
        # FPADD 通常只有 VLD
        meta = {"loops": 0, "inc": 0}
        self.check_unit_symmetry("FPADD", meta)

    def test_fpmul_symmetry(self):
        meta = {"loops": 10, "inc": 0, "precision": "f32"}
        self.check_unit_symmetry("FPMUL", meta)

    def test_rtovr_symmetry(self):
        meta = {"slot_id": 3, "loops": 63, "inc": 0}
        self.check_unit_symmetry("RTOVR", meta)

if __name__ == "__main__":
    unittest.main()
