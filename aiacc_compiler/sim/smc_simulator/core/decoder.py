from typing import Dict, Any, Union
from aiacc_hardware.src.spec_loader import UnitSpec

class DecodedInstruction:
    """
    通用解码结果对象。
    支持通过属性访问字段，例如 instr.ADD_SUB。
    """
    def __init__(self, fields: Dict[str, Any]):
        self.__dict__['_fields'] = fields

    def __getattr__(self, name: str) -> Any:
        if name in self._fields:
            return self._fields[name]
        raise AttributeError(f"微指令中不存在字段: {name}")

    def __getitem__(self, key: str) -> Any:
        return self._fields[key]
    
    def to_dict(self) -> Dict[str, Any]:
        return self._fields

class InstructionDecoder:
    """
    通用指令解码器。
    根据 UnitSpec 自动计算位宽偏移并解析二进制微指令 (LSB 对齐)。
    布局: [Header (41b)][Payload (Variable)]
    """
    # 标准 SMI-IQ Header 字段定义 (由高向低物理顺序，但解析时需考虑 LSB Offset)
    # 按照 emission.py 中的顺序: 
    # [0-5]: INC_EMBED, [6-9]: INC, [10-12]: DLY, [13-19]: COND, 
    # [20-29]: LOOPS, [30-35]: EMBED_END, [36-38]: EMBED_LOOPS, 
    # [39]: JUMP, [40]: VLD
    def __init__(self, spec, unit_name: str = None, system_spec=None):
        self.spec = spec
        self.unit_name = unit_name or getattr(spec, "name", "UNKNOWN")
        # Load header layout dynamically
        if system_spec is None:
            from aiacc_hardware.src.spec_loader import SpecLoader
            system_spec = SpecLoader().get_system_core()
            
        self._header_layout = []
        if system_spec:
            iq_header = system_spec.get("iq_header", [])
            for item in iq_header:
                self._header_layout.append((item.get("field"), item.get("width")))
        
        self._layout = self._calculate_layout()

    @property
    def payload_width(self) -> int:
        """该单元微指令的 payload 实际位宽（不含 header）。"""
        fields = getattr(self.spec, "code_fields", getattr(self.spec, "micro_instructions", []))
        return sum(getattr(f, "bits", getattr(f, "width", 0)) for f in fields)

    @property
    def header_width(self) -> int:
        """IQ header 总位宽。"""
        return sum(w for _, w in self._header_layout)

    @property
    def total_width(self) -> int:
        """单条指令的完整位宽 = payload + header。"""
        return self.payload_width + self.header_width

    def _calculate_layout(self) -> Dict[str, Dict[str, Any]]:
        """实现 LSB 对齐的布局计算 (物理注册表优先版)"""
        layout = {}
        curr_payload = 0
        
        # 1. 计算 Payload 注册位宽或求和位宽
        fields = getattr(self.spec, "code_fields", getattr(self.spec, "micro_instructions", []))
        spec_sum = sum(getattr(f, "bits", getattr(f, "width", 0)) for f in fields)
        reg_width = 0
        if hasattr(self.spec, "name"):
            # 搜索注册表数据 (通过 system_spec)
            pass # 此处通常已知注册表
        
        # 通过 spec 内部注册名检索
        # 由于 spec_loader 加载的 UnitSpec 可能不直接带注册表，我们在此使用回退与强制校准
        # 注意：这里的 self.spec.unit_name 必须与其注册名一致
        # 🛸 物理纠偏：Payload 位宽应始终由字段定义求和得出 (spec_sum)
        # 绝不能使用 iq_reg (那是硬件 ID)，否则会导致 Header 偏移错位。
        final_payload_width = spec_sum
            
        # print(f"[DEC-AUDIT] Unit:{self.spec.unit_name} RegistryWidth:{reg_width} SpecSum:{spec_sum} -> P_Width:{final_payload_width}")
            
        # 2. 映射 Payload 字段
        curr = 0
        fields = getattr(self.spec, "code_fields", getattr(self.spec, "micro_instructions", []))
        for field in fields:
            f_bits = getattr(field, "bits", getattr(field, "width", 0))
            layout[field.name] = {
                "offset": curr,
                "mask": (1 << f_bits) - 1,
                "spec": field
            }
            curr += f_bits
            
        # 3. Header 紧跟在【物理决议】后的 Payload 之后
        curr = final_payload_width
        for name, width in self._header_layout:
            layout[name] = {
                "offset": curr,
                "mask": (1 << width) - 1,
                "spec": None
            }
            curr += width
            
        return layout


    def decode(self, raw_bits: int) -> DecodedInstruction:
        """
        全量解码：将 128-bit 原始比特解析为语义对象
        """
        fields = {}
        # print(f"[DECODER-DEBUG] Decoding raw bits: {hex(raw_bits)}")
        for name, info in self._layout.items():
            val = (raw_bits >> info["offset"]) & info["mask"]
            
            # 处理有符号数 (目前仅 INC)
            if name == "INC" and (val & 0x8):
                val -= 16
                
            # 语义映射 (安全访问)
            spec_obj = info["spec"]
            encoding = getattr(spec_obj, "encoding", None)
            if spec_obj and encoding and val in encoding:
                fields[name] = encoding[val]
            else:
                fields[name] = val
            
            # 🛸 关键调试：仅打印非零的核心控制字段或特定的 Unit 调试
            # if val != 0 or name in ["VLD", "DLY", "LOOPS", "EMBED_END"]:
                # print(f"  [FIELD] {name:<10} | Offset: {info['offset']:<2} | Val: {val}")
                # pass
                
        return DecodedInstruction(fields)

if __name__ == "__main__":
    # 简单的本地测试逻辑
    from aiacc_hardware.src.spec_loader import SpecLoader
    loader = SpecLoader()
    spec = loader.get_unit("INTADD")
    if spec:
        decoder = InstructionDecoder(spec)
        # 构造一个 12bit 左右的测试指令 (VALID=1, ADD_SUB=1[SUB])
        # bit0: VALID
        # ...
        # [Step 1.1 的顺序中 ADD_SUB 是最后一个，即总宽度 13 的最高位]
        test_bits = 1 | (1 << 12) 
        instr = decoder.decode(test_bits)
        print(f"Decoded VALID: {instr.VALID}")
        print(f"Decoded ADD_SUB: {instr.ADD_SUB}")
