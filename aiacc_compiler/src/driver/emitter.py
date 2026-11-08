import os, json, struct, numpy as np, copy
from pathlib import Path
from typing import List, Dict, Tuple, Any

from api.hw_spec import HardwareSpec
from driver.field_mapper import FieldMapper
from driver.compactor import Compactor
from driver.packer import BitPacker
from driver.hw_constants import (
    IQ_BASE_UR_READ, IQ_BASE_UR_WRITE, 
    PORT_OFFSET_UR_RDATA, PORT_ID_FPMUL_D, PORT_ID_FPADD_D, PORT_ID_INTADD_D0,
    OFFSET_VLD, DEFAULT_BITS_CODE, DEFAULT_MAX_CYCLES
)
from ir.lir.lir_op import PhysicalInstruction, SMIHeader

class LIRPacker:
    """
    AIACC AI-COMPILER EMITTER (V5.1: Fully Decoupled Serialization).
    """
    def __init__(self, config_dir: str = "hw_config", verbosity: int = 2):
        self.hw_spec = HardwareSpec.load(config_dir)
        self.verbosity = verbosity

    def emit_from_instructions(self, instructions: List[PhysicalInstruction], case_root: str, program=None):
        root = Path(case_root)
        smc0 = root / "smc_0"
        ur_dir, bank_dir, analysis = smc0/"ur_data", smc0/"output_bank", smc0/"analysis"
        for d in [smc0, ur_dir, bank_dir, analysis]: os.makedirs(d, exist_ok=True)
        
        for old_hex in smc0.glob("iq_*.hex"):
            old_hex.unlink()
        
        (root / "sim_trace.log").touch()
        
        iq_buckets: Dict[str, List[PhysicalInstruction]] = {}
        for instr in instructions:
            iq_buckets.setdefault(instr.iq_name, []).append(instr)

        iq_init = {}
        for iq_name, stream in iq_buckets.items():
            stream.sort(key=lambda x: x.abs_t)
            lookup_type = self._get_lookup_type(iq_name)
            total_bits = BitPacker.get_unit_bits(self.hw_spec, lookup_type)
            hex_chars = (total_bits + 3) // 4
            
            hex_lines = []
            for instr in stream:
                is_body_stage = (instr.stage == "BODY")
                val = BitPacker.pack_capsule(
                    spec=self.hw_spec,
                    unit_type=lookup_type,
                    payload_dict=instr.payload,
                    is_body=is_body_stage,
                    **instr.header.to_dict()
                )
                hex_lines.append(f"{val:0{hex_chars}x}")
            
            hex_file_name = f"iq_{iq_name}.hex"
            with open(smc0 / hex_file_name, "w") as f:
                f.write("\n".join(hex_lines) + "\n")
            
            iq_init[iq_name.replace("_", "-")] = f"smc_0/{hex_file_name}"

        # 🛸 [V5.1 FIX] 动态获取 Bank 数量，不再死磕 range(8)
        bank_num = getattr(self.hw_spec.params, "PARAM_UR_BANK_NUM", 8)
        sim_conf = {
            "smc_id": 0,
            "iq_init": iq_init,
            "ur_init": {str(b): f"smc_0/ur_data/bank{b}.hex" for b in range(bank_num)},
            "max_cycles": DEFAULT_MAX_CYCLES,
            "tensors": []
        }
        
        if program:
            self._handle_program_data(program, ur_dir, sim_conf)

        with open(root / "sim_config.json", "w") as f:
            json.dump(sim_conf, f, indent=2)

    def _get_lookup_type(self, iq_name: str) -> str:
        """
        [Phase 3] 代理至 UnitPayloadFactory 处理，摒弃硬编码字典。
        """
        from src.driver.payload_factory import UnitPayloadFactory
        return UnitPayloadFactory.resolve_iq_type(iq_name, self.hw_spec)

    def _handle_program_data(self, program, ur_dir, sim_conf):
        """处理 Bank 数据初始化与 Tensor 元数据映射。"""
        v_lanes = program.vector_lanes
        element_bits = getattr(program, "element_bits", 32)
        
        # 🛸 [V6.7 FIX] 动态选择打包格式与移位参数，支持 FP32/FP16/INT8 等全精度类型，彻底拆除硬件打包硬编码
        if element_bits == 32:
            pack_fmt, unpack_fmt, bit_shift, hex_len_factor = '<f', '<I', 32, 8
        elif element_bits == 16:
            # Python 3.6+ 支持 '<e' 表达 16-bit 浮点数，'<H' 表达 16-bit 无符号短整型
            pack_fmt, unpack_fmt, bit_shift, hex_len_factor = '<e', '<H', 16, 4
        elif element_bits == 8:
            pack_fmt, unpack_fmt, bit_shift, hex_len_factor = '<b', '<B', 8, 2
        else:
            pack_fmt, unpack_fmt, bit_shift, hex_len_factor = '<f', '<I', 32, 8 # fallback

        for b_id, storage in program.memory_allocator.banks.items():
            with open(ur_dir / f"bank{b_id}.hex", "w") as f:
                for entry in storage:
                    # 假定每个 word 是 v_lanes 个元素
                    # 🛸 动态打包各精度向量
                    hex_val = 0
                    for i in range(v_lanes):
                        val_bits = struct.unpack(unpack_fmt, struct.pack(pack_fmt, entry[v_lanes-1-i]))[0]
                        hex_val = (hex_val << bit_shift) | val_bits
                    f.write(f"{hex_val:0{v_lanes*hex_len_factor}x}\n")
        
        for name, obj in program.tensors.items():
            sim_conf["tensors"].append({
                "name": name,
                "bank": obj.bank,
                "addr": obj.offset,
                "shape": list(obj.shape),
                "role": obj.role
            })
