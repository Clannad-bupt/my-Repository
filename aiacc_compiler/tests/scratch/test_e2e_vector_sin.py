"""
E2E Physical Simulation Test: Adaptive Vector sin(a[N])  V5 (Final)
====================================================================
正确流程：
  1. 用 op_tester 完整运行单批（自动分配 + SMT + Emit + 生成 bank*.hex）
  2. 直接复用已生成的 bank*.hex 作为 loop 仿真的初始内存（内存布局已正确）
  3. 把 x[4:8] 追加写入 bank_x 的 addr+1 位置（唯一需要扩展的行）
  4. 注入 outer_loops/inc → LoopStitcher → 新 acb_bundle_loop
  5. 用修改后的 bank*.hex 运行 SMC → 验证 y[0:8] bit-perfect
"""
import sys, os, struct, json, shutil
import numpy as np
from pathlib import Path

sys.path.insert(0, os.path.join(os.getcwd(), "src"))

from core.tiling import TilingPass
from orchestrator.loop_stitcher import LoopStitcher
from library.atomic.transcendentals.sin import get_sin_standard_dfg
from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from api.op_tester import OperatorPhysicalTester
from driver.emitter import LIRPacker


def sin_horner(x):
    x2 = x * x
    return x * (1.0 - x2 * (1.0/6.0 - x2 * (1.0/120.0)))


def read_hex_bank(hex_path: Path) -> list:
    """把 bankX.hex 读回为 [[f32,f32,f32,f32], ...] 列表。"""
    rows = []
    for line in hex_path.read_text().strip().splitlines():
        line = line.strip()
        if not line: continue
        packed = int(line, 16)
        row = []
        for i in range(4):
            bits = (packed >> (32 * i)) & 0xFFFFFFFF
            val  = struct.unpack('>f', struct.pack('>I', bits))[0]
            row.append(val)
        rows.append(row)
    return rows


def write_hex_bank(hex_path: Path, rows: list):
    """把 [[f32,f32,f32,f32], ...] 写回为 bankX.hex。"""
    lines = []
    for row in rows:
        packed = 0
        for i, x in enumerate(row[:4]):
            bits = int(struct.unpack('>I', struct.pack('>f', float(x)))[0]) & 0xFFFFFFFF
            packed |= (bits << (32 * i))
        lines.append(f"{packed:032x}")
    hex_path.write_text("\n".join(lines) + "\n")


def test_vector_sin_e2e(N: int = 8):
    vec_width = 4
    tiling    = TilingPass(vec_width).tile(N)   # K=2, r=0

    x_data = np.linspace(0.1, 0.8, N, dtype=np.float32)
    golden = sin_horner(x_data)

    print(f"\n[TEST] sin(a[{N}]): K={tiling.K}, r={tiling.r}")
    print(f"[TEST] x = {x_data}")
    print(f"[TEST] golden = {golden}")

    # ──────────────────────────────────────────────────────────
    # Step 1: op_tester 单批完整仿真（生成 bank*.hex + dfg 分配回填）
    # ──────────────────────────────────────────────────────────
    BUILD = Path("build/scratch/vec_sin_v5")
    tester = OperatorPhysicalTester(hw_config_path="hw_config", build_root=str(BUILD))
    dfg    = get_sin_standard_dfg()

    golden_single = golden[0:4]
    acb_single    = BUILD / "sin/perf_contest/balanced/phase2_emission/acb_bundle"
    acb_single.mkdir(parents=True, exist_ok=True)
    np.savez(acb_single / "golden_data.npz", y=golden_single)

    success, msg = tester.run_full_stack_verification(
        op_name="sin", dfg_ops=dfg,
        input_data={"x_in": x_data[0:4]}, golden_y=golden_single,
        strategies=["balanced"],
    )
    assert success, f"单批仿真失败: {msg}"
    print(f"\n[TEST] ✅ 单批 sin(x[0:4]) PASS")

    # ──────────────────────────────────────────────────────────
    # Step 2: 找到 x_in 节点的实际 bank/addr（从 dfg 节点 metadata）
    # ──────────────────────────────────────────────────────────
    x_node   = next(op for op in dfg if op.metadata.get("role") == "operand_a")
    out_node = next(op for op in dfg if op.metadata.get("role") == "output")
    bank_x, addr_x = x_node.metadata["bank_id"], x_node.metadata["addr"]
    bank_y, addr_y = out_node.metadata["bank_id"], out_node.metadata["addr"]
    print(f"[TEST] x: Bank{bank_x}/Addr{addr_x}  y: Bank{bank_y}/Addr{addr_y}")

    # ──────────────────────────────────────────────────────────
    # Step 3: 复制 bank*.hex → acb_bundle_loop，再追加 x[4:8]
    # ──────────────────────────────────────────────────────────
    acb_loop = BUILD / "sin/perf_contest/balanced/phase2_emission/acb_bundle_loop"
    ur_src   = acb_single / "smc_0/ur_data"
    ur_dst   = acb_loop   / "smc_0/ur_data"
    ur_dst.mkdir(parents=True, exist_ok=True)

    for b in range(8):
        src_hex = ur_src / f"bank{b}.hex"
        dst_hex = ur_dst / f"bank{b}.hex"
        if src_hex.exists():
            shutil.copy(src_hex, dst_hex)
        else:
            dst_hex.write_text("\n".join(["00" * 16] * 64) + "\n")

    # 追加第二批 x 数据到 bank_x 的 addr_x+1 行
    bank_x_rows = read_hex_bank(ur_dst / f"bank{bank_x}.hex")
    while len(bank_x_rows) <= addr_x + 1:
        bank_x_rows.append([0.0] * 4)
    bank_x_rows[addr_x + 1] = x_data[4:8].tolist()
    write_hex_bank(ur_dst / f"bank{bank_x}.hex", bank_x_rows)
    print(f"[TEST] Patched bank{bank_x}.hex: addr={addr_x}={x_data[0:4].tolist()}, addr={addr_x+1}={x_data[4:8].tolist()}")

    # ──────────────────────────────────────────────────────────
    # Step 4: 注入 outer_loops/inc → LoopStitcher → re-SMT
    # ──────────────────────────────────────────────────────────
    for op in dfg:
        if op.metadata.get("role") in ("operand_a", "output"):
            op.metadata["outer_loops"] = tiling.K - 1
            op.metadata["outer_inc"]   = 1

    spec       = HardwareSpec.load("hw_config")
    smt_result = SMTSynthesizer(spec).solve(dfg)
    assert smt_result, "SMT UNSAT!"
    schedule, II = smt_result["schedule"], smt_result["II"]

    ls = LoopStitcher(spec)
    ls.stitch_vectorized("sin", dfg, schedule, II, tiling)
    final_stream = ls.base_stitcher.get_final_stream()
    print(f"[TEST] LoopStitcher: {len(final_stream)} MIR ops, II={II}")

    # ──────────────────────────────────────────────────────────
    # Step 5: Emit → 复用 ur_data（ProgramProxy 传入已有数据）
    # ──────────────────────────────────────────────────────────
    # 从文件重建 banks dict
    banks = {}
    for b in range(8):
        banks[b] = read_hex_bank(ur_dst / f"bank{b}.hex")

    tensors_meta = [
        {"name": "x", "bank": bank_x, "addr": addr_x, "offset": addr_x,
         "shape": [2, 4], "role": "input"},
        {"name": "y", "bank": bank_y, "addr": addr_y, "offset": addr_y,
         "shape": [2, 4], "role": "output"},
    ]

    class ProgramProxy:
        def __init__(self, b, t):
            self.memory_allocator = type("A", (), {"banks": b})()
            self.tensors = {ti["name"]: type("T", (), ti) for ti in t}

    # Emit（会重写 ur_data/）
    acb_loop.mkdir(parents=True, exist_ok=True)
    LIRPacker("hw_config").emit_from_stream(
        final_stream, str(acb_loop),
        program=ProgramProxy(banks, tensors_meta)
    )

    np.savez(acb_loop / "golden_data.npz", y=golden.reshape(2, 4))
    print(f"[TEST] Golden shape=(2,4) saved")

    # ──────────────────────────────────────────────────────────
    # Step 6: SMC 物理仿真
    # ──────────────────────────────────────────────────────────
    import subprocess
    ws  = Path.cwd()
    env = os.environ.copy()
    env["PYTHONPATH"] = f"{ws}:{ws}/src:{ws}/sim:" + env.get("PYTHONPATH", "")
    runner = ws / "sim" / "smc_simulator" / "tools" / "physical_runner.py"

    res = subprocess.run(["python3", str(runner), str(acb_loop)],
                         env=env, capture_output=True, text=True)
    log = res.stdout + res.stderr
    (acb_loop.parent / "sim_loop.log").write_text(log)

    print("\n[SIM]")
    for line in log.splitlines():
        if any(k in line for k in ["UR-WRITE", "MUL-EXEC", "PASSED", "FAILED",
                                    "Golden", "Sample", "Diff", "Verifying"]):
            print(f"  {line}")

    passed = "PASSED" in log or "Verified successfully" in log
    print("\n" + "="*60)
    if passed:
        print(f"✅ E2E PASSED  sin(a[{N}])  Loop×{tiling.K}  Bit-Perfect!")
    else:
        print(f"❌ E2E FAILED  sin(a[{N}])")
    print("="*60)
    return passed


if __name__ == "__main__":
    ok = test_vector_sin_e2e(N=8)
    sys.exit(0 if ok else 1)
