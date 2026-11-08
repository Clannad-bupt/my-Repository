import os
import sys
import unittest
from pathlib import Path

ws = Path(__file__).parent.parent.parent
sys.path.append(str(ws / "src"))
sys.path.append(str(ws / "sim"))

from api.hw_spec import HardwareSpec
from ir.mir.mir_op import MIROp
from driver.field_mapper import FieldMapper
from driver.packer import BitPacker
from smc_simulator.core.decoder import InstructionDecoder
from aiacc_hardware.src.spec_loader import SpecLoader

class TestSoftwareArchitectureCoverage(unittest.TestCase):
    """
    AIACC 软件架构两阶段全覆盖单元测试。
    测试目标：确保从算法语义（Algorithm）到微指令定义，再到后端打包（Hex Packing）与硬件仿真器（Simulator）之间严格的物理位级对齐。
    """
    @classmethod
    def setUpClass(cls):
        # 加载硬件五源数据规范
        hw_config_path = ws / "hw_config"
        cls.spec = HardwareSpec.load(str(hw_config_path))
        cls.spec_loader = SpecLoader()

    # =========================================================================
    # Part 1: 算法到后端微指令定义的测试 (Hardware Units Full Coverage)
    # 目的：验证 MIROp 的语义信息（metadata）是否正确地被 FieldMapper 转换为目标模块的有效 Payload。
    # =========================================================================

    def _verify_algo_to_definition(self, op_type, metadata, expected_fields):
        """通用测试套件：验证给定的算子语义是否正确投射为物理位域负载。"""
        # 1. 构造 MIROp (Algorithm semantic)
        op = MIROp(trace_id=f"test_{op_type}", op_type=op_type, logic_cycle=0, operands=[], metadata=metadata)
        
        # 2. 获取硬件架构中该算子的物理结构 (Micro-instruction definition)
        # 支持 RTOVR-N 格式的动态映射
        query_type = op_type
        if "RTOVR" in op_type: query_type = "RTOVR"
        elif "UR_READ" in op_type: query_type = "UR_READ"
        elif "UR_WRITE" in op_type: query_type = "UR_WRITE"
            
        module_spec = self.spec.modules.get(query_type)
        self.assertIsNotNone(module_spec, f"Module Spec for {query_type} not found")
        
        # 3. 映射 Payload
        payload = FieldMapper.map_op_to_payload(op, module_spec)
        
        # 4. 反向验证位域 (反切分)
        pos = 0
        decoded_fields = {}
        for field in module_spec.code_fields:
            fname = field.name
            fbits = field.bits
            mask = (1 << fbits) - 1
            decoded_fields[fname] = (payload >> pos) & mask
            pos += fbits
            
        # 5. 断言关键字段正确性
        for k, v in expected_fields.items():
            self.assertIn(k, decoded_fields, f"Field '{k}' not in generated payload")
            self.assertEqual(decoded_fields[k], v, f"Field '{k}' decoding mismatch in {op_type}")

    def test_algo_to_def_fpmul(self):
        self._verify_algo_to_definition(
            op_type="FPMUL",
            metadata={"src_prec": "f32", "dst_prec": "f32"},
            expected_fields={"VALID": 1, "SRC_PREC": 1, "DST_PREC": 1}
        )

    def test_algo_to_def_fpadd(self):
        self._verify_algo_to_definition(
            op_type="FPADD",
            metadata={"src_prec": "f32", "dst_prec": "f32"},
            expected_fields={"VALID": 1, "SRC_PREC": 1, "DST_PREC": 1, "SUB": 0}
        )

    def test_algo_to_def_urr(self):
        self._verify_algo_to_definition(
            op_type="UR_READ",
            metadata={"bank_id": 4, "p_addr": 128},
            expected_fields={"VALID": 1, "BANK": 4, "ADDR": 128}
        )

    def test_algo_to_def_urw(self):
        self._verify_algo_to_definition(
            op_type="UR_WRITE",
            metadata={"bank_id": 7, "p_addr": 255},
            expected_fields={"VALID": 1, "BANK": 7, "ADDR": 255} 
        )

    def test_algo_to_def_rtovr(self):
        self._verify_algo_to_definition(
            op_type="RTOVR-3", 
            metadata={"slot_id": 3},
            expected_fields={"VALID": 1, "SEL": 3}
        )

    def test_algo_to_def_logic(self):
        self._verify_algo_to_definition(
            op_type="LOGIC",
            metadata={"OP_TYPE": "SELECT_GT", "PRECISION": "PREC_32"},
            expected_fields={"VALID": 1, "OP_TYPE": 5, "PRECISION": 1}
        )

    # =========================================================================
    # Part 2: 后端驱动到微指令打包的测试 (Micro-instruction Hex Packing Full Coverage)
    # =========================================================================

    def _verify_driver_to_packing(self, unit_name, payload, header_kwargs):
        """通用测试套件：验证打包后的位串在仿真器内解析出的各字段是否符合预期。"""
        # 1. 映射单位名称到物理队列名称
        u_base = unit_name.upper()
        if "UR-R" in u_base: u_base = "UR_READ"
        elif "UR-W" in u_base: u_base = "UR_WRITE"
        elif "RTOVR" in u_base: u_base = "RTOVR"
        
        # 2. 加载硬件 IQ 定义和仿真器 Decoder
        iq_conf = self.spec.iqs.get(f"IQ_{u_base}")
        self.assertIsNotNone(iq_conf, f"IQ Config for {unit_name} not found")
        u_spec = self.spec_loader.get_unit(u_base.lower())
        self.assertIsNotNone(u_spec, f"Unit Spec for {u_base} not found")
        
        decoder = InstructionDecoder(u_spec)
        
        # 3. 通过 Backend Driver (BitPacker) 进行 128-bit 指令打包
        hex_val = BitPacker.pack_capsule(
            iq_name=unit_name, 
            iq_conf=iq_conf, 
            payload=payload, 
            **header_kwargs
        )
        
        # 4. 利用仿真器的原生物理解码器进行截取解析
        decoded_instr = decoder.decode(hex_val)
        
        # 验证物理 Header 对齐 (利用解码器返回的语义字段)
        self.assertEqual(decoded_instr.VLD, 1, "VLD bit mismatch")
        self.assertEqual(decoded_instr.LOOPS, header_kwargs.get("loops", 0), "Loops field mismatch")
        self.assertEqual(decoded_instr.DLY, header_kwargs.get("dly", 0), "DLY field mismatch")
        self.assertEqual(decoded_instr.INC, header_kwargs.get("inc", 0) if header_kwargs.get("inc", 0) >= 0 else header_kwargs.get("inc", 0), "INC field mismatch")
        
        # 验证物理 Payload 位截取
        fields = decoded_instr.to_dict()
        
        pos = 0
        for field in u_spec.micro_instructions:
             fname = field.name
             fbits = field.width
             mask = (1 << fbits) - 1
             expected_val = (payload >> pos) & mask
             
             if field.encoding and expected_val in field.encoding:
                 expected_val = field.encoding[expected_val]
                 
             self.assertEqual(fields.get(fname), expected_val, f"Payload field '{fname}' corruption in {unit_name}")
             pos += fbits

    def test_packing_fpmul(self):
        op = MIROp(trace_id="test", op_type="FPMUL", logic_cycle=0, operands=[], metadata={"src_prec":"f32","dst_prec":"f32"})
        payload = FieldMapper.map_op_to_payload(op, self.spec.modules["FPMUL"])
        self._verify_driver_to_packing("fpmul", payload=payload, header_kwargs={"loops": 5, "dly": 0, "inc": 0})

    def test_packing_fpadd(self):
        op = MIROp(trace_id="test", op_type="FPADD", logic_cycle=0, operands=[], metadata={"src_prec":"f32","dst_prec":"f32"})
        payload = FieldMapper.map_op_to_payload(op, self.spec.modules["FPADD"])
        self._verify_driver_to_packing("fpadd", payload=payload, header_kwargs={"loops": 1, "dly": 3, "inc": 1})

    def test_packing_urr(self):
        op = MIROp(trace_id="test", op_type="UR_READ", logic_cycle=0, operands=[], metadata={"bank_id": 4, "p_addr": 128})
        payload = FieldMapper.map_op_to_payload(op, self.spec.modules["UR_READ"])
        self._verify_driver_to_packing("ur-r0", payload=payload, header_kwargs={"loops": 63, "dly": 0, "inc": 0})

    def test_packing_urw(self):
        op = MIROp(trace_id="test", op_type="UR_WRITE", logic_cycle=0, operands=[], metadata={"bank_id": 7, "p_addr": 255})
        payload = FieldMapper.map_op_to_payload(op, self.spec.modules["UR_WRITE"])
        self._verify_driver_to_packing("ur-w0", payload=payload, header_kwargs={"loops": 0, "dly": 7, "inc": 2})

    def test_packing_rtovr(self):
        op = MIROp(trace_id="test", op_type="RTOVR-3", logic_cycle=0, operands=[], metadata={"slot_id": 3})
        payload = FieldMapper.map_op_to_payload(op, self.spec.modules["RTOVR"])
        self._verify_driver_to_packing("rtovr-3", payload=payload, header_kwargs={"loops": 4, "dly": 0, "inc": 0})

    def test_packing_logic(self):
        # 🚀 深度验证 BITS_CODE 与 Payload 位宽对齐性
        iq_conf = self.spec.iqs.get("IQ_LOGIC")
        mod_spec = self.spec.modules.get("LOGIC")
        payload_width = sum(f.bits for f in mod_spec.code_fields)
        
        # 🛸 关键断言：BITS_CODE 必须等于 Payload 位宽，否则 Header 偏移会错位
        self.assertEqual(iq_conf.BITS_CODE, payload_width, 
                         f"IQ_LOGIC BITS_CODE ({iq_conf.BITS_CODE}) mismatch payload width ({payload_width})")

        op = MIROp(trace_id="test", op_type="LOGIC", logic_cycle=0, operands=[], metadata={"OP_TYPE": "SELECT_GT", "precision": "f32"})
        payload = FieldMapper.map_op_to_payload(op, mod_spec)
        self._verify_driver_to_packing("logic", payload=payload, header_kwargs={"loops": 0, "dly": 0, "inc": 0})

if __name__ == '__main__':
    unittest.main()
