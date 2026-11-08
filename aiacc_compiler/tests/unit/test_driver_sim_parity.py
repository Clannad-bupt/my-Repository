import pytest
import numpy as np
from pathlib import Path

# 导入编译器后端模块
from api.hw_spec import HardwareSpec
from ir.mir.mir_op import MIROp
from driver.field_mapper import FieldMapper
from driver.packer import BitPacker
from driver.hw_constants import DEFAULT_BITS_CODE

# 导入仿真器解码器 (Loopback 验证)
from smc_simulator.core.decoder import InstructionDecoder

@pytest.fixture
def hw_spec():
    return HardwareSpec.load("hw_config")

def test_instruction_codec_parity_coverage(hw_spec):
    """
    全模块指令打包/解析对齐性测试 (Parity Test)。
    验证：FieldMapper + BitPacker == InstructionDecoder
    """
    print("\n" + "="*80)
    print(f"{'Module':<15} | {'Field':<15} | {'Original':<10} | {'Decoded':<10} | {'Status'}")
    print("-" * 80)

    # 需要进行覆盖性测试的模块列表
    modules_to_test = ["UR_READ", "UR_WRITE", "RTOVR", "FPADD", "FPMUL", "INTADD"]
    
    any_failed = False
    
    for m_name in modules_to_test:
        # 1. 获取硬件规范
        mod_spec = hw_spec.modules.get(m_name)
        if not mod_spec: continue
        
        # 2. 构造具有代表性的测试元数据
        test_meta = {
            "bank_id": 3,
            "p_addr": 127,
            "precision": "f32",
            "p_src_id": 31,     # RtoVR 物理端子
            "slot_id": 5,      # RtoVR Slot
            "wbe": 0xAAAA,     # Write Byte Enable
            "smc_id_en": 1
        }
        op = MIROp(m_name, 0, f"test_{m_name}", metadata=test_meta)
        
        # 3. 编译器后端：打包打包
        payload = FieldMapper.map_op_to_payload(op, mod_spec)
        
        # 🛸 物理对齐：获取该模块注册的 BITS_CODE (从 IQ 配置中获取，而非注册表 ID)
        lookup = f"IQ_{m_name}"
        iq_conf = hw_spec.iqs.get(lookup)
        if not iq_conf: continue
        bits_code = iq_conf.BITS_CODE
        
        # 执行 128-bit 封装 (包含 Header)
        full_command = BitPacker.pack_capsule(
            iq_name=m_name.lower(), iq_conf=iq_conf, payload=payload, loops=0,
            hw_spec=hw_spec
        )
        # 显式点火 VLD (Bit = bits_code + 40)
        full_command |= (1 << (bits_code + 40))

        # 4. 仿真器前端：解码解码
        decoder = InstructionDecoder(mod_spec, unit_name=m_name)
        decoded_instr = decoder.decode(full_command)
        
        # 5. 字段级逐一对比
        # 提取解码出的字段字典
        decoded_dict = decoded_instr.to_dict()
        
        # 我们重点对比 Payload 区域的相关业务字段
        for field in mod_spec.code_fields:
            fname = field.name
            decoded_val = decoded_dict.get(fname)
            
            # --- 解析预期的原始逻辑值 ---
            # (借用 FieldMapper 的语义逻辑进行真值提取以方便对比)
            # 这里我们手动根据字段名查找真值
            expected_val = None
            if fname in ["re", "we", "vld", "VALID", "VLD"]: expected_val = 1
            elif fname in ["rbank", "wbank", "bank_id", "bank", "p_bank_id", "BANK"]: expected_val = test_meta["bank_id"]
            elif fname in ["raddr", "waddr", "p_addr", "address", "ADDR"]: expected_val = test_meta["p_addr"]
            elif fname in ["src_id", "dr_index", "SRC_ID", "SEL"]: expected_val = test_meta["p_src_id"]
            elif fname in ["add_or_sub", "SUB", "add_sub"]: expected_val = 1 if "SUB" in m_name else 0
            elif fname == "wbe": expected_val = test_meta["wbe"]
            elif "smc_id" in fname: expected_val = test_meta["smc_id_en"]
            
            # 状态标记：支持语义等价性对比 (int vs string)
            status = "PASS"
            if expected_val is not None:
                # 获取原义
                encoding = field.encoding
                sem_val = encoding.get(expected_val, str(expected_val))
                
                # 比较 (支持 int==int, string==string, int=="str_of_int")
                is_match = False
                if str(decoded_val).upper() == str(sem_val).upper():
                    is_match = True
                elif isinstance(decoded_val, int) and decoded_val == expected_val:
                    is_match = True
                    
                if not is_match:
                    status = "!!!! FAIL !!!!"
                    any_failed = True
            
            # 打印对比行 (仅打印我们已知预期的字段)
            if expected_val is not None:
                orig_label = field.encoding.get(expected_val, hex(expected_val))
                deco_str = str(decoded_val)
                print(f"{m_name:<15} | {fname:<15} | {orig_label:<10} | {deco_str:<10} | {status}")

    print("="*80)
    assert not any_failed, "探测到编译器打包字段与仿真器解码字段不一致！请检查位偏移。"

if __name__ == "__main__":
    pytest.main([__file__, "-s"])
