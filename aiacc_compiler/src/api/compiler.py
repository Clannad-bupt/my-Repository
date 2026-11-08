from typing import List, Dict, Optional
import os
from api.hw_spec import HardwareSpec
from orchestrator.stitcher import HSTSStitcher
from driver.emitter import LIRPacker

class LIRCompiler:
    """
    AIACC V5.0 统一编译器 API。
    实现从 DFG 逻辑流向物理 41-bit 二进制流的全量生产。
    """
    def __init__(self, hw_config_dir: str):
        self.spec = HardwareSpec.load(hw_config_dir)
        self.packer = LIRPacker(hw_config_dir)

    def compile(self, op_blocks: List[Dict], output_dir: str):
        print(f"\n[AIACC END-TO-END COMPILATION] Starting Batch for {len(op_blocks)} Operators...")
        stitcher = HSTSStitcher()
        for block in op_blocks:
            stitcher.add_operator(
                name=block["name"],
                stream=block["stream"],
                synthesis_res=block["synthesis_res"],
                depends_on=block.get("depends_on")
            )
        final_stream = stitcher.get_final_stream()
        self.packer.emit_from_stream(final_stream, output_dir)
        print(f"✅ AIACC: Pipeline Emission Complete ({len(final_stream)} LIR Items).")
        return len(final_stream)
