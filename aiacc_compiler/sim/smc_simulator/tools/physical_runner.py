import sys
import logging
import json
import struct
import numpy as np
import argparse
import traceback
from pathlib import Path
import csv

def load_sim_config(case_dir: Path):
    with open(case_dir / "sim_config.json") as f:
        return json.load(f)

def _float32_to_ur128(float_array: np.ndarray) -> dict:
    # Converts float32 array to 128-bit words (4 floats per word)
    flat_data = float_array.flatten()
    words = {}
    for i in range(0, len(flat_data), 4):
        chunk = flat_data[i:i+4]
        word_val = 0
        for j, val in enumerate(chunk):
            bits = struct.unpack(">I", struct.pack(">f", val))[0]
            word_val |= (bits << (j * 32))
        words[i // 4] = word_val
    return words

def poison_memory(kernel, config: dict, case_dir: Path):
    """
    Load data from UR config and input_data.npz.
    Poison unallocated memory with NaNs.
    """
    import math
    
    # 1. Fill entire UR memory with NaN equivalent pattern (e.g. 0x7FC00000 -> IEEE-754 NaN)
    nan_bits = struct.unpack(">I", struct.pack(">f", float('nan')))[0]
    nan_word = (nan_bits) | (nan_bits << 32) | (nan_bits << 64) | (nan_bits << 96)
    
    for bank_id in range(len(kernel.ur_bank.storage)):
        for addr in range(len(kernel.ur_bank.storage[bank_id])):
            kernel.ur_bank.storage[bank_id][addr] = nan_word

    # 2. Load actual initial data
    ur_init_map = config.get("ur_init", {})
    for bank_str, rel_path in ur_init_map.items():
        bank_id = int(bank_str)
        file_path = case_dir / rel_path
        if file_path.exists():
            with open(file_path, "r") as f:
                lines = f.read().splitlines()
                for i, line in enumerate(lines):
                    if line.strip():
                        if i >= len(kernel.ur_bank.storage[bank_id]):
                            break
                        val = int(line.strip(), 16)
                        kernel.ur_bank.storage[bank_id][i] = val

def run_physical_verification(case_dir: str):
    case_path = Path(case_dir)
    if not case_path.exists():
         print(f"Error: {case_dir} does not exist.")
         return False

    # Force import from root, sim and src directories
    root = Path.cwd()
    sim_dir = root / "sim"
    src_dir = root / "src"
    if str(root) not in sys.path:
        sys.path.insert(0, str(root))
    if str(sim_dir) not in sys.path:
        sys.path.insert(0, str(sim_dir))
    if str(src_dir) not in sys.path:
        sys.path.insert(0, str(src_dir))

    import smc_simulator.core.kernel as kernel_mod
    ParallelKernel = kernel_mod.ParallelKernel

    config = load_sim_config(case_path)
    kernel = ParallelKernel()

    # Enable simulation tracing
    trace_file = case_path / "sim_trace.log"
    kernel.enable_trace()

    # Phase 1: Decoupled Cold Start & Instruction Fetch
    iq_map = {n: str(case_path / h) for n, h in config.get("iq_init", {}).items()}
    # We load programs directly using paths
    kernel.load_program(iq_map, {}) # Only load iq, UR is done via poisoning manually

    # Phase 1.3: Memory Poisoning
    poison_memory(kernel, config, case_path)

    # Phase 2.1: Execution
    print(f"Starting Physical Simulation for {case_dir}...")
    
    # Configure logging to file and stdout
    file_handler = logging.FileHandler(trace_file, mode='w')
    file_handler.setFormatter(logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s'))
    logging.getLogger("ParallelKernel").addHandler(file_handler)
    stream_handler = logging.StreamHandler(sys.stdout)
    stream_handler.setFormatter(logging.Formatter('%(message)s'))
    logging.getLogger("ParallelKernel").addHandler(stream_handler)
    logging.getLogger("ParallelKernel").setLevel(logging.INFO)

    class Tee:
        def __init__(self, *files): self.files = files
        def write(self, obj):
            for f in self.files: f.write(obj); f.flush()
        def flush(self):
            for f in self.files: f.flush()

    try:
        max_cycles = config.get("max_cycles", 1000)
        with open(trace_file, "a") as f_trace:
            import contextlib
            with contextlib.redirect_stdout(Tee(sys.stdout, f_trace)):
                kernel.run(max_cycles=max_cycles)
        
        # [Standard Alignment] Phase 2.1.2: Analysis and Traces
        analysis_dir = case_path / "smc_0" / "analysis"
        analysis_dir.mkdir(parents=True, exist_ok=True)
        
        # Dump timeline and events
        kernel.trace_recorder.save_to_timeline_ascii(str(analysis_dir / "timeline_ascii.txt"))
        if hasattr(kernel.trace_recorder, 'save_to_chrome_tracing'):
             kernel.trace_recorder.save_to_chrome_tracing(str(analysis_dir / "sim_events.json"))
             
        # [Standard Alignment] Phase 2.1.3: Output Bank State
        output_bank_dir = case_path / "smc_0" / "output_bank"
        kernel.dump_banks(str(output_bank_dir))
        
    except Exception as e:
        print(f"Simulation Halted: {e}")
        traceback.print_exc()
        return False
        
    print(f"Simulation Finished at Cycle {kernel.cycle_count}. Verifying Data...")

    # Phase 2.2 & 2.3: Fetching results and Golden Check
    golden_file = case_path / "golden_data.npz"
    if not golden_file.exists():
         print(f"Golden data not found at {golden_file}")
         return False
         
    goldens = np.load(golden_file)
    all_passed = True
    
    for t_info in config.get("tensors", []):
        if t_info.get("role") != "output":
            continue
            
        name = t_info["name"]
        bank = int(t_info["bank"])
        addr = int(t_info["addr"])
        shape = tuple(t_info["shape"])
        dtype = t_info.get("dtype", "float32")
        
        size = int(np.prod(shape))
        if name not in goldens:
            continue
            
        ref = goldens[name]
        
        sim_words = []
        for i in range((size + 3) // 4):
            val = kernel.ur_bank.storage[bank][addr + i]
            sim_words.append(val)
            
        sim_flat = []
        for i in range(size):
            bits = (sim_words[i // 4] >> ((i % 4) * 32)) & 0xFFFFFFFF
            # unpack handles NaNs safely
            f_val = struct.unpack(">f", struct.pack(">I", bits))[0]
            sim_flat.append(f_val)
            
        sim_np = np.array(sim_flat, dtype=np.float32).reshape(shape)
        
        # 🛸 ADPC-ALIGN: 支持边界值测试中的 NaN 对比。
        # 只有当 sim_np 有 NaN 但 ref 没有时，才考虑为“内存污染”。
        sim_flat_ar = sim_np.flatten()
        ref_flat = ref.flatten()
        
        mask_sim_nan = np.isnan(sim_flat_ar)
        mask_ref_nan = np.isnan(ref_flat)
        
        if np.any(mask_sim_nan & (~mask_ref_nan)):
             print(f"FAILED {name}: Unexpected NaN detected! (Poisoned Read)")
             all_passed = False
             continue

        # Mathematical tolerance check
        atol = 5e-3 # Global tolerance
        passed = True
        if not np.allclose(sim_flat_ar, ref_flat, atol=atol, equal_nan=True):
            max_err = np.max(np.abs(sim_flat_ar - ref_flat))
            print(f"FAILED {name}: Max Difference = {max_err}")
            all_passed = False
            passed = False
        else:
            print(f"PASSED {name}: Verified successfully against Golden Data.")
            
        print(f"      [Sample View] Golden Data Vs. SMC Output (first 5 elements):")
        sample_len = min(5, size)
        sim_sample = sim_np.flatten()[:sample_len]
        ref_sample = ref.flatten()[:sample_len]
        for idx in range(sample_len):
            print(f"       [{idx}]: Golden={ref_sample[idx]:.5f}, SMC={sim_sample[idx]:.5f} | Diff={abs(ref_sample[idx] - sim_sample[idx]):.6f}")
        print("-" * 50)
        
        # 核心追加：导出 128-bit HEX 按行文件，用于跟硬件 RTL / FPGA 强行拉平比对 (Hardware Diff)
        dump_file = case_path / f"ur_bank_{bank}_out_{name}.hex"
        with open(dump_file, "w") as f_hex:
            for w_val in sim_words:
                f_hex.write(f"{w_val:032x}\n")
        print(f"      [Hardware Diff] Exported RTL-compatible SMC output to: {dump_file.name}")
            
    return all_passed

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="AIACC Physical Runner Testbench")
    parser.add_argument("case_dir", type=str, help="Path to the test output directory (e.g. test_output/e2e_sin_v3)")
    args = parser.parse_args()
    
    success = run_physical_verification(args.case_dir)
    sys.exit(0 if success else 1)
