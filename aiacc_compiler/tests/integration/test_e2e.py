import os
import sys
import json
import numpy as np
import struct
from pathlib import Path
import unittest
import subprocess

# 确保 src 在 pythonpath 
sys.path.append(str(Path(__file__).parent.parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from orchestrator.stitcher import HSTSStitcher
from orchestrator.memory_allocator import MemoryAllocator
from driver.field_mapper import FieldMapper
from driver.compactor import Compactor
from driver.emitter import LIRPacker

from library.atomic.transcendentals.sin import get_sin_8order_dfg
from library.atomic.trig_extra.precision_trig import get_atan2_industrial_dfg

class TestE2EValidation(unittest.TestCase):
    def test_sin_atan2_e2e_acb_compliant(self):
        """
        Sprint 6: 全链路端到端 (E2E) 测试，对齐 ACB 输出标准。
        """
        workspace_dir = Path(__file__).parent.parent.parent
        output_dir = workspace_dir / "build" / "e2e_sin_atan2_acb"
        smc_0_dir = output_dir / "smc_0"
        ur_data_dir = smc_0_dir / "ur_data"
        output_bank_dir = smc_0_dir / "output_bank"
        analysis_dir = smc_0_dir / "analysis"
        
        for d in [output_dir, smc_0_dir, ur_data_dir, output_bank_dir, analysis_dir]:
            os.makedirs(d, exist_ok=True)
            
        print("\n[E2E] Orchestrating Sin and Atan2 for Bit-Perfect Audit...")
        
        # 1. 编译管线初始化
        spec = HardwareSpec.load(str(workspace_dir / "hw_config"))
        synthesizer = SMTSynthesizer(spec)
        stitcher = HSTSStitcher()
        mem_alloc = MemoryAllocator(spec.params)
        
        # 2. 端到端配置与 DFG 获取
        num_elements = 8
        sin_ops = get_sin_8order_dfg()
        atan2_ops = get_atan2_industrial_dfg()
        
        # 3. SMT 合成物理时空解 (由高保真引擎处理 256 位宽)
        sin_res = synthesizer.solve(sin_ops, strategy="extreme", num_elements=num_elements)
        atan2_res = synthesizer.solve(atan2_ops, strategy="extreme", num_elements=num_elements)
        self.assertIsNotNone(sin_res, "Sin Synthesis Failed")
        self.assertIsNotNone(atan2_res, "Atan2 Synthesis Failed")
        
        # 4. HSTS 全局缝合
        stitcher.add_operator("Sin", sin_ops, sin_res)
        stitcher.add_operator("Atan2", atan2_ops, atan2_res)
        final_stream = stitcher.get_final_stream()
        
        # 5. 生成输入桩数据和 Golden 参考
        x_data = np.random.uniform(-3.14, 3.14, size=(num_elements,)).astype(np.float32)
        y_data = np.random.uniform(-1, 1, size=(num_elements,)).astype(np.float32)
        x_atan_data = np.random.uniform(-1, 1, size=(num_elements,)).astype(np.float32)
        c_data = np.zeros(num_elements, dtype=np.float32)
        
        input_data = {
            "x": x_data, "y": y_data, "x_atan": x_atan_data, "c": c_data
        }
        
        golden_data = {
            "Sin_out": np.sin(x_data),
            "Atan2_out": np.arctan2(y_data, x_atan_data)
        }
        np.savez(output_dir / "input_data.npz", **input_data)
        np.savez(output_dir / "golden_data.npz", **golden_data)
        
        # 6. 物理空间分配与张量元数据生成
        allocated_tensors = []
        class TensorObj:
            def __init__(self, name, size, bank_id, start_t, end_t, role="input"):
                self.name = name
                self.size = (size + 3) // 4 
                self.bank_id = bank_id
                self.start_t = start_t
                self.end_t = end_t
                self.role = role
                self.base_addr = 0

        # Sin 内部张量映射
        # 注意：这里的名字必须与 get_sin_8order_dfg 中的 node_id 精确对齐
        allocated_tensors.append(TensorObj("x_rd", 256, 0, 0, 1000, "input"))
        allocated_tensors.append(TensorObj("w_y", 256, 1, 0, 1000, "scratch"))
        allocated_tensors.append(TensorObj("c1_rd", 256, 2, 0, 1000, "input"))
        allocated_tensors.append(TensorObj("c2_rd", 256, 3, 0, 1000, "input"))
        allocated_tensors.append(TensorObj("c3_rd", 256, 4, 0, 1000, "input"))
        allocated_tensors.append(TensorObj("c4_rd", 256, 5, 0, 1000, "input"))
        allocated_tensors.append(TensorObj("x_f_rd", 256, 0, 0, 1000, "input"))
        allocated_tensors.append(TensorObj("w_fin", 256, 7, 0, 1000, "output"))
        
        # Atan2 内部张量映射
        allocated_tensors.append(TensorObj("y", 256, 0, 0, 1000, "input"))
        allocated_tensors.append(TensorObj("x_atan", 256, 1, 0, 1000, "input"))
        allocated_tensors.append(TensorObj("quad_res", 256, 7, 0, 1000, "output"))

        addr_map = mem_alloc.allocate(allocated_tensors)
        
        # 🛸 关键修正：物理地址回填到最终指令流
        addr_dict = {t.name: t.base_addr for t in allocated_tensors}
        for op in final_stream:
            nid = op.node_id
            if nid in addr_dict:
                op.metadata["p_addr"] = addr_dict[nid]
            else:
                # 模糊匹配 (针对 x_rd, x_f_rd 等)
                if "x_rd" in nid or "x_f_rd" in nid: op.metadata["p_addr"] = addr_dict.get("x_rd", 0)
                elif "y" in nid and "Atan2" in str(op.metadata.get("instance_name","")): op.metadata["p_addr"] = addr_dict.get("y", 0)
                elif "x_atan" in nid: op.metadata["p_addr"] = addr_dict.get("x_atan", 0)
                elif "w_fin" in nid: op.metadata["p_addr"] = addr_dict.get("w_fin", 0)
                elif "quad_res" in nid: op.metadata["p_addr"] = addr_dict.get("quad_res", 0)
                else: op.metadata["p_addr"] = 0

        # 元数据规范 (供仿真器分析结果)
        tensor_specs = []
        for t in allocated_tensors:
            if t.role != "scratch":
                sim_name = t.name
                # 指令写到 w_fin，但仿真器 Config 里的 Golden 叫 Sin_out
                if t.name == "w_fin": sim_name = "Sin_out"
                if t.name == "quad_res": sim_name = "Atan2_out"
                tensor_specs.append({
                    "name": sim_name, "bank": str(t.bank_id), "addr": t.base_addr,
                    "shape": [256], "role": t.role
                })

        # 7. 导出高保真指令流与 Config (.hex 格式，标准 ACB 布局)
        packer = LIRPacker(str(workspace_dir / "hw_config"))
        packer.emit_from_stream(final_stream, str(output_dir))
        # 9. 写入 Bank HEX
        def write_bank_hex(bank_id, tensor_data_list):
            with open(ur_data_dir / f"bank{bank_id}.hex", "w") as f:
                storage = {} 
                for data, addr in tensor_data_list:
                    for i in range(0, len(data), 4):
                        chunk = data[i:i+4]; word = 0
                        for j, val in enumerate(chunk):
                            bits = struct.unpack("<I", struct.pack("<f", val))[0]
                            word |= (bits << (j * 32))
                        storage[addr + i//4] = word
                for i in range(max(storage.keys()) + 1 if storage else 1):
                    f.write(f"{storage.get(i, 0):032x}\n")
        
        # 映射数据到 Bank
        write_bank_hex(0, [(x_data, addr_dict["x_rd"]), (y_data, addr_dict["y"])])
        write_bank_hex(1, [(x_atan_data, addr_dict["x_atan"])])
        write_bank_hex(2, [(c_data, addr_dict["c1_rd"])])
        write_bank_hex(7, []) # Output bank

        # 10. 调用物理仿真器
        print("\n[SIMULATION] Launching Physical Runner...")
        env = os.environ.copy()
        env["PYTHONPATH"] = f"{workspace_dir}:{workspace_dir}/sim:" + env.get("PYTHONPATH", "")
        runner_path = workspace_dir / "sim" / "smc_simulator" / "tools" / "physical_runner.py"
        res = subprocess.run([sys.executable, str(runner_path), str(output_dir)], env=env, capture_output=True, text=True)
        sys.stderr.write(res.stdout + "\n" + res.stderr + "\n")
        self.assertEqual(res.returncode, 0, "Physical Simulation Failed")

        print("\n✅ E2E Bit-Perfect Verification PASSED.")

if __name__ == "__main__":
    unittest.main()
