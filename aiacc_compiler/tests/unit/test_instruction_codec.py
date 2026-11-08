import unittest
import sys
from pathlib import Path

# 对齐工程路径
ws = Path(__file__).parent.parent.parent
sys.path.append(str(ws / "src"))
sys.path.append(str(ws / "sim"))

from api.hw_spec import HardwareSpec
from driver.field_mapper import FieldMapper
from driver.packer import BitPacker
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader
from ir.mir.mir_op import MIROp

class TestInstructionCodec(unittest.TestCase):
    """
    SMC 物理指令编解码对齐性单元测试 (Authoritative Version)。
    确保编译器生成的 BitStream 与 仿真器解析出的 Semantic Fields 100% 物理对齐。
    """
    @classmethod
    def setUpClass(cls):
        cls.hw_config_path = ws / "hw_config"
        cls.spec = HardwareSpec.load(str(cls.hw_config_path))
        cls.spec_loader = SpecLoader()

    def _test_unit_codec(self, unit_type, metadata, header_config):
        """测试单个硬件单元的编解码闭环。"""
        # 1. 查找硬件定义
        lookup_type = unit_type.upper()
        if "UR-R" in lookup_type: lookup_type = "UR_READ"
        elif "UR-W" in lookup_type: lookup_type = "UR_WRITE"
        elif "RTOVR" in lookup_type: lookup_type = "RTOVR"

        mod_spec = self.spec.modules.get(lookup_type)
        iq_conf = self.spec.iqs.get(f"IQ_{lookup_type}")
        
        self.assertIsNotNone(mod_spec, f"Module spec for {lookup_type} missing")
        self.assertIsNotNone(iq_conf, f"IQ config for {lookup_type} missing")

        # 🛸 关键验证：BITS_CODE 必须与 Payload 位宽对齐 (防止错位)
        payload_width = sum(f.bits for f in mod_spec.code_fields)
        self.assertEqual(iq_conf.BITS_CODE, payload_width, 
                         f"FATAL ALIGNMENT ERROR: {lookup_type} IQ.BITS_CODE({iq_conf.BITS_CODE}) != PayloadWidth({payload_width}). "
                         "This will cause header fields (VLD/LOOPS/...) to be misplaced in hardware memory.")

        # 2. 模拟编译器映射 Payload
        op_type = unit_type
        if "RTOVR" in unit_type: op_type = f"RTOVR-{metadata.get('slot_id', 0)}"
        op = MIROp(trace_id="codec_test", op_type=op_type, logic_cycle=0, operands=[], metadata=metadata)
        payload = FieldMapper.map_op_to_payload(op, mod_spec)

        # 3. 通过 BitPacker 打包
        packed_val = BitPacker.pack_capsule(
            iq_name=unit_type.lower(),
            iq_conf=iq_conf,
            payload=payload,
            hw_spec=self.spec,
            **header_config
        )

        # 4. 通过 Simulator InstructionDecoder 解码
        # 注意：此处必须使用 SpecLoader 加载的硬件原语，以确保与仿真器行为一致
        u_prim = self.spec_loader.get_unit(lookup_type.lower())
        decoder = InstructionDecoder(u_prim)
        instr = decoder.decode(packed_val)

        # 5. 断言 Header 字段正确性
        self.assertEqual(int(instr.VLD), header_config.get("valid", 1), f"{unit_type} VLD mismatch")
        self.assertEqual(int(instr.LOOPS), header_config.get("loops", 0), f"{unit_type} LOOPS mismatch")
        self.assertEqual(int(instr.DLY), header_config.get("dly", 0), f"{unit_type} DLY mismatch")
        self.assertEqual(int(instr.INC), header_config.get("inc", 0), f"{unit_type} INC mismatch")

        # 6. 断言 Payload 字段正确性
        decoded_dict = instr.to_dict()
        pos = 0
        for f in mod_spec.code_fields:
            expected_raw = (payload >> pos) & ((1 << f.bits) - 1)
            # 处理 Encoding 映射
            expected_val = expected_raw
            if f.encoding and expected_raw in f.encoding:
                 expected_val = f.encoding[expected_raw]
            
            self.assertEqual(decoded_dict.get(f.name), expected_val, f"{unit_type} Payload field '{f.name}' mismatch")
            pos += f.bits

    def test_codec_fpmul(self):
        self._test_unit_codec("fpmul", {"src_prec": "f32", "dst_prec": "f32"}, {"loops": 10, "dly": 3})

    def test_codec_fpadd(self):
        self._test_unit_codec("fpadd", {"src_prec": "f16", "dst_prec": "f32", "SUB": "SUB"}, {"valid": 1, "inc": 2})

    def test_codec_logic(self):
        # 🚀 重点测试之前出错的 LOGIC 算子 (SELECT_GT)
        self._test_unit_codec("logic", {"OP_TYPE": "SELECT_GT", "precision": "f32", "smc_id": 4}, {"loops": 0, "dly": 0})

    def test_codec_urr(self):
        self._test_unit_codec("ur-r1", {"bank_id": 2, "p_addr": 64}, {"loops": 63})

    def test_codec_urw(self):
        self._test_unit_codec("ur-w3", {"bank_id": 5, "p_addr": 127, "wbe": 0xFFFF}, {"inc": 1})

    def test_codec_rtovr(self):
        self._test_unit_codec("rtovr-13", {"slot_id": 13}, {"valid": 1})

if __name__ == "__main__":
    unittest.main()
