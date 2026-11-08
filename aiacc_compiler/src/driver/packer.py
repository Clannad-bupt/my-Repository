from typing import List, Dict, Any

# 🛸 唯一保留的静态定义：IQ Header (由于其结构在 isa_v5 层面是全局锁死的)
ISA_V5_HEADER = {
    "INC_EMBED": (0, 6), "INC": (6, 4), "DLY": (10, 3), "COND": (13, 7), 
    "LOOPS": (20, 10), "EMBED_END": (30, 6), "EMBED_LOOPS": (36, 3), 
    "JUMP": (39, 1), "VLD": (40, 1)
}

class BitPacker:
    """
    AIACC 绝对位打包引擎 (V7.0 - SSoT 规格对齐版)。
    实时从 HardwareSpec 提取位偏移，拒绝任何形式的硬编码字典。
    """
    @staticmethod
    def pack_capsule(spec: Any, unit_type: str, payload_dict: Dict[str, Any], is_body: bool = True, **header_kwargs) -> int:
        u_type = unit_type.upper().replace("-", "_")
        # 兼容性映射
        if "UR_R" in u_type: u_type = "UR_READ"
        if "UR_W" in u_type: u_type = "UR_WRITE"
        
        # 🛸 核心：从传入的 spec 中动态获取模块定义
        if u_type not in spec.modules:
            body_bits = 16
            b_fields = []
        else:
            module = spec.modules[u_type]
            body_bits = module.code_bits
            b_fields = module.code_fields # 这是一个 [{name, bits}, ...] 列表

        # 1. 打包 Header (41 bits)
        h_val = 0
        norm_header = {k.upper(): v for k, v in header_kwargs.items()}
        for f_name, (start, width) in ISA_V5_HEADER.items():
            val = norm_header.get(f_name, 0)
            h_val |= (int(val) & ((1 << width) - 1)) << start
        
        # 2. 打包 Body (动态推导偏移)
        b_val = 0
        if is_body:
            norm_body = {k.upper(): v for k, v in payload_dict.items()}
            curr_pos = 0
            for field in b_fields:
                f_name = field.name.upper()
                f_width = field.bits
                f_val = norm_body.get(f_name, 0)
                b_val |= (int(f_val) & ((1 << f_width) - 1)) << curr_pos
                curr_pos += f_width

        # 3. 组合封装 (Header 在高位)
        return (h_val << body_bits) | b_val

    @staticmethod
    def get_unit_bits(spec: Any, unit_type: str) -> int:
        u_type = unit_type.upper().replace("-", "_")
        if "UR_R" in u_type: u_type = "UR_READ"
        if "UR_W" in u_type: u_type = "UR_WRITE"
        
        body_bits = spec.modules[u_type].code_bits if u_type in spec.modules else 16
        return 41 + body_bits
