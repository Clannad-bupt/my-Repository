import os, sys, json, subprocess
import numpy as np
from typing import List, Dict, Any
from api.hw_spec import HardwareSpec
from synthesis.ucode_solver import UCodeSynthesizer
from driver.assembler import PhysicalAssembler
from driver.emitter import LIRPacker
from collections import namedtuple

TensorObj = namedtuple("TensorObj", ["bank", "offset", "shape", "role"])

class PipelineRunner:
    def __init__(self, hw_config_name: str = "hw_config", show_review: bool = True, show_model: bool = False):
        self.hw_config_name = hw_config_name
        self.spec = HardwareSpec.load(hw_config_name)
        self.show_review = show_review
        self.show_model = show_model
        self.output_root = "build/verification_v2"
        os.makedirs(self.output_root, exist_ok=True)

    def run_full_stack(self, operator: Any, input_data: Dict[str, np.ndarray], op_name: str = None):
        if self.show_review:
            print(f"\n>>> Starting Pipeline Runner...")
        
        # 🛸 [V5.4 FIX] 注入硬件 Spec 和数据类型到模拟内存层
        from verification_v2.infra.mock_memory import MockProgram
        program = MockProgram(spec=self.spec, dtype=operator.dtype)
        
        vector_width_bits = self.spec.params.PARAM_SMC_VECTOR_WIDTH
        element_bits = operator.element_bits
        vector_lanes = vector_width_bits // element_bits
        
        ops = operator.get_dfg(spec=self.spec)
        if self.show_model:
            print("\n[DEBUG] DFG Nodes Metadata:")
            for op in ops:
                print(f"  Node: {op.trace_id:<15} | Type: {op.op_type:<10} | Meta: {op.metadata}")
                
        synth = UCodeSynthesizer(self.spec)
        result = synth.solve_with_units(ops, MII=1, MAX_II=64)
        if not result:
            raise RuntimeError("Synthesis failed.")
        
        schedule, II, unit_map = result["schedule"], result["II"], result.get("unit_assignments", {})
        for op in ops:
            op.metadata["global_t"] = schedule[op.trace_id]
            op.metadata["unit_id"] = unit_map.get(op.trace_id)
 
        v_level = 3 if self.show_model else (2 if self.show_review else 1)
        assembler = PhysicalAssembler(self.spec, verbosity=v_level)
        
        input_tensor_names = [n for n, c in operator.tensor_configs.items() if c.get("role") == "input"]
        if not input_tensor_names:
             input_key = next(iter(input_data))
        else:
             input_key = input_tensor_names[0]
             
        num_loop_iters = len(input_data[input_key]) // vector_lanes
        physical_instrs = assembler.assemble(ops, II=II, num_iterations=num_loop_iters)
 
        operator.inject_data(program, input_data, vector_lanes=vector_lanes, spec=self.spec)
        golden_res = operator.get_golden(input_data, vector_lanes=vector_lanes)
        
        for name, config in operator.tensor_configs.items():
            raw_shape = config.get("shape", (num_loop_iters, vector_lanes))
            t_shape = tuple((vector_lanes if s == -1 else s) for s in raw_shape)
            program.tensors[name] = TensorObj(
                bank=config["bank"], 
                offset=config.get("offset", 0), 
                shape=t_shape,
                role=config["role"]
            )
 
        if op_name is None:
            op_name = operator.__class__.__name__.lower()
        output_dir = os.path.join(self.output_root, op_name)
        os.makedirs(output_dir, exist_ok=True)
        
        actual_max_t = max((instr.abs_t for instr in physical_instrs), default=0)
        safe_max_cycles = actual_max_t + (II * num_loop_iters) + 50
        if self.show_review:
            print(f"[RUNNER] Dynamic Max Cycles set to: {safe_max_cycles}")

        v_level = 3 if self.show_model else (2 if self.show_review else 1)
        packer = LIRPacker(self.hw_config_name, verbosity=v_level)
        packer.emit_from_instructions(physical_instrs, output_dir, program=program)
        self._patch_max_cycles(output_dir, safe_max_cycles)
        
        # 🛸 [V5.4 FIX] 保存物理指令供审计员使用
        with open(os.path.join(output_dir, "physical_instructions.json"), "w") as f:
            json.dump([instr.to_dict() for instr in physical_instrs], f, indent=2)
        
        np.savez(os.path.join(output_dir, "golden_data.npz"), y=golden_res["y"])
        with open(os.path.join(output_dir, "expected_values.json"), "w") as f:
            json.dump(golden_res["intermediates"], f, indent=2)
        
        schedule_dump = {
            "II": II,
            "ops": {op.trace_id: {"t": int(op.metadata["global_t"]), "iq": op.metadata["unit_id"]} for op in ops}
        }
        with open(os.path.join(output_dir, "compiler_schedule.json"), "w") as f:
            json.dump(schedule_dump, f, indent=2)

        if self.show_review:
            print(f"[STEP] Running Physical Simulation...")
        
        log_path = os.path.join(output_dir, f"{op_name}_sim.log")
        with open(log_path, "w") as log_f:
            sim_cmd = [sys.executable, "sim/smc_simulator/tools/physical_runner.py", output_dir]
            res = subprocess.run(sim_cmd, stdout=log_f, stderr=log_f)
            if res.returncode != 0:
                print(f"[ERROR] Physical simulation failed. Check {log_path}")

        # 🛸 [V6.4] 解析并控制台实时回显 SMC 仿真比对结果
        if os.path.exists(log_path):
            with open(log_path, "r") as f:
                lines = f.readlines()
            start_idx = -1
            for idx, line in enumerate(lines):
                if "Simulation Finished" in line and "Verifying Data..." in line:
                    start_idx = idx
                    break
            if start_idx != -1:
                print("\n>>> SMC Physical Simulation Numerical Verification:")
                for line in lines[start_idx:]:
                    print("  " + line.strip("\n"))
            else:
                # Fallback: Just print last 10 lines if "Simulation Finished" is not matched
                print("\n>>> SMC Physical Simulation Numerical Verification:")
                for line in lines[-10:]:
                    print("  " + line.strip("\n"))

        return {
            "output_dir": output_dir,
            "log_path": log_path,
            "schedule": schedule,
            "II": II,
            "physical_instrs": physical_instrs,
            "num_iterations": num_loop_iters,
            "sim_success": (res.returncode == 0),
            "ops": ops
        }

    def _patch_max_cycles(self, output_dir: str, max_cycles: int):
        conf_path = os.path.join(output_dir, "..", "sim_config.json")
        if not os.path.exists(conf_path):
            conf_path = os.path.join(output_dir, "sim_config.json")
            if not os.path.exists(conf_path): return
        
        with open(conf_path, "r") as f:
            data = json.load(f)
        data["max_cycles"] = max_cycles
        with open(conf_path, "w") as f:
            json.dump(data, f, indent=2)

    def parse_simulation_timeline(self, output_dir: str) -> Dict[str, List[str]]:
        timeline_path = os.path.join(output_dir, "smc_0", "analysis", "timeline_ascii.txt")
        actual_activity = {}
        if os.path.exists(timeline_path):
            with open(timeline_path, "r") as f:
                for line in f:
                    if "|" in line and "iq_name" not in line and "occupancy" not in line:
                        parts = line.split("|")
                        if len(parts) >= 2:
                            name = parts[0].strip()
                            trace = parts[1].strip().split()
                            actual_activity[name] = trace
        return actual_activity
