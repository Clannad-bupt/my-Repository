#!/usr/bin/env python3
"""端到端(README §13 步骤 12,三期-FP8 验收):examples/llama_mlp_fp8.py → minic → minirt。

  图:rms_norm(f16) → quant(f8) → quant_matmul(gate) → silu
      → quant_matmul(up) ⊙ → mul → quant(f8) → quant_matmul(down),
      同 §14.1 shape(S=32,H=256,I=512)。
  覆盖:f8E4M3FN 容器 ODS/verifier、|u1 位直通常量段、per-tensor sb(len-1)
        的 stride-0 广播 DMA 与 (1,) vec 视图(§6.6)、GEMM_F8(A/B=e4m3、
        C=f32 累加)、V_QUANT f8 编码、V_DEQUANT f32 源。

  数值断言:vs torch FP32 黄金(权重未量化):cos ≥ 0.99、rel-l2 ≤ 1.2e-1。
    说明:Gaussian 权重上 FP8 per-tensor 劣于 INT8 per-channel(cos
    0.998/rel-L2 6.5e-2 vs INT8 的 0.9998/2.5e-2)是预期且如实入表——
    e4m3 尾数 3 bit,权重/激活量化误差 ~2-3% RMS 链式积累;FP8 的正确
    卖点是离群值鲁棒(单测 test_numerics_fp8.py 的交叉点,README §6.6)。
  性能断言:ΣGEMM_F8 cycles ≤ 0.55 × Σmacs/128 —— fp8 峰值 256 macs/cyc
    恰为 f16(128)的 2×(spec 键 macs_per_cycle_fp8),实测 ratio=0.500。
"""
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIC = os.path.join(REPO, "build", "minic")
MINIRT = os.path.join(REPO, "build", "minirt")
SPEC = os.path.join(REPO, "configs", "npu256.json")
COMPARE = os.path.join(REPO, "scripts", "compare_golden.py")
EXAMPLE = os.path.join(REPO, "examples", "llama_mlp_fp8.py")

S, H, I = 32, 256, 512


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0, f"cmd failed: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}"
    return r


def test_mlp_fp8(tmp):
    exp = os.path.join(tmp, "mlp_fp8")
    run([sys.executable, EXAMPLE, "--export", exp])
    for f in ("mlp_fp8.mlir", "manifest.json", "ref_fp32.npy",
              "x_fp16.npy", "sa_g.npy", "w_gate_q.npy", "sb_g.npy"):
        assert os.path.exists(os.path.join(exp, f)), f"缺导出产物 {f}"

    bin_path = os.path.join(exp, "mlp_fp8.minisa")
    run([MINIC, "--input", os.path.join(exp, "mlp_fp8.mlir"),
         "--emit", bin_path, "--spec", SPEC,
         "--manifest", os.path.join(exp, "manifest.json")])

    with open(bin_path[:-7] + ".meta.json") as f:
        meta = json.load(f)
    # 7 个权重(gamma + 3×(w_q, sb))进常量段,其中 w_q 为 |u1 → f8 位直通;
    # x/sa_g/sa_d 为运行期输入
    assert len(meta["consts"]) == 7, f"consts={len(meta['consts'])}"
    # sb 为 len-1 per-tensor:stride-0 DRAM 视图(§6.6)
    assert meta["views"], "len-1 sb 应产生 stride-0 广播视图(§6.6)"

    out_path = os.path.join(tmp, "out_fp8.npy")
    tl_path = os.path.join(tmp, "tl_fp8.json")
    run([MINIRT, "--bin", bin_path, "--spec", SPEC,
         "--dump-out", out_path, "--dump-timeline", tl_path])

    run([sys.executable, COMPARE, out_path,
         "--ref", os.path.join(exp, "ref_fp32.npy"),
         "--cos", "0.99", "--rel-l2", "1.2e-1"])

    # --- 性能:GEMM_F8 实测周期 vs f16 峰值模型(macs/128)---
    with open(tl_path) as f:
        ev = json.load(f)["traceEvents"]
    gemms = [e for e in ev if e["name"] == "GEMM_F8"]
    assert gemms, "timeline 必须含 GEMM_F8"
    macs = sum(e["args"]["flops"] for e in gemms) // 2
    cyc_f8 = sum(e["dur"] for e in gemms)
    cyc_f16_model = macs / 128  # f16 峰值 128 macs/cyc(§3 spec)
    ratio = cyc_f8 / cyc_f16_model
    assert ratio <= 0.55, f"GEMM_F8 不足 2× 加速:ratio={ratio:.3f}"
    exp_macs = 2 * S * H * I + S * I * H  # gate + up + down
    assert macs == exp_macs, f"macs={macs} ≠ {exp_macs}"
    print(f"  GEMM_F8: {cyc_f8} cyc ({macs} macs, "
          f"{macs / cyc_f8:.0f} macs/cyc) vs f16 峰值模型 "
          f"{cyc_f16_model:.0f} cyc → 加速比 {1 / ratio:.2f}×")
    print("PASS test_mlp_fp8 (e2e: fp8 mlir → minisa → sim → numpy)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_mlp_fp8(tmp)
    print("ALL E2E FP8 TESTS PASS")
