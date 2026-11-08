#!/usr/bin/env python3
"""端到端(README §13 步骤 11,二期-INT8 验收):examples/llama_mlp_int8.py → minic → minirt。

  图:rms_norm → quant → quant_matmul(gate) → silu → quant_matmul(up)
      → mul → quant → quant_matmul(down),同 §14.1 shape(S=32,H=256,I=512)。
  覆盖:quant/quant_matmul lowering、GEMM_I8(i32 精确累加)调度、
        V_QUANT/V_DEQUANT、i8 常量段打包、per-token/per-channel scale。

  数值断言:vstorch FP32 黄金(权重未量化!,故意比 §7 f16 用例更严格):
    cos ≥ 0.999、rel-l2 ≤ 3e-2。
    说明:plain W8A8 absmax 对称量化的理论量级 ≈ 每 GEMM ~1% 量化噪声、
    三个量化点链式积累 ≈ 2.2-2.5e-2(numpy 纯量化语义全链路 2.51e-2,
    设备实测 2.50e-2,重合即证明链路实现无额外误差);2e-2 以下在
    absmax scale 下不可达,改进方向见 README §19(clip/calibration)。
  性能断言:ΣGEMM_I8 cycles ≤ 0.55 × Σmacs/128 —— i8 峰值 256 macs/cyc
    恰为 f16(128)的 2×,实测 ratio=0.500。
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
EXAMPLE = os.path.join(REPO, "examples", "llama_mlp_int8.py")

S, H, I = 32, 256, 512


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0, f"cmd failed: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}"
    return r


def test_mlp_i8(tmp):
    exp = os.path.join(tmp, "mlp_i8")
    run([sys.executable, EXAMPLE, "--export", exp])
    for f in ("mlp_int8.mlir", "manifest.json", "ref_fp32.npy",
              "x_fp16.npy", "sa_g.npy", "sa_d.npy", "w_gate_q.npy"):
        assert os.path.exists(os.path.join(exp, f)), f"缺导出产物 {f}"

    bin_path = os.path.join(exp, "mlp_int8.minisa")
    run([MINIC, "--input", os.path.join(exp, "mlp_int8.mlir"),
         "--emit", bin_path, "--spec", SPEC,
         "--manifest", os.path.join(exp, "manifest.json")])

    with open(bin_path[:-7] + ".meta.json") as f:
        meta = json.load(f)
    # 7 个权重(gamma + 3×(w_q, sb))进常量段;x/sa_g/sa_d 为运行期输入
    assert len(meta["consts"]) == 7, f"consts={len(meta['consts'])}"

    out_path = os.path.join(tmp, "out.npy")
    tl_path = os.path.join(tmp, "tl.json")
    run([MINIRT, "--bin", bin_path, "--spec", SPEC,
         "--dump-out", out_path, "--dump-timeline", tl_path])

    run([sys.executable, COMPARE, out_path,
         "--ref", os.path.join(exp, "ref_fp32.npy"),
         "--cos", "0.999", "--rel-l2", "3e-2"])

    # --- 性能:GEMM_I8 实测周期 vs f16 峰值模型(macs/128)---
    with open(tl_path) as f:
        ev = json.load(f)["traceEvents"]
    gemms = [e for e in ev if e["name"] == "GEMM_I8"]
    assert gemms, "timeline 必须含 GEMM_I8"
    macs = sum(e["args"]["flops"] for e in gemms) // 2
    cyc_i8 = sum(e["dur"] for e in gemms)
    cyc_f16_model = macs / 128  # f16 峰值 128 macs/cyc(§3 spec)
    ratio = cyc_i8 / cyc_f16_model
    assert ratio <= 0.55, f"GEMM_I8 不足 2× 加速:ratio={ratio:.3f}"
    exp_macs = 2 * S * H * I + S * I * H  # gate + up + down
    assert macs == exp_macs, f"macs={macs} ≠ {exp_macs}"
    print(f"  GEMM_I8: {cyc_i8} cyc ({macs} macs, "
          f"{macs / cyc_i8:.0f} macs/cyc) vs f16 峰值模型 "
          f"{cyc_f16_model:.0f} cyc → 加速比 {1 / ratio:.2f}×")
    print("PASS test_mlp_i8 (e2e: quant mlir → minisa → sim → numpy)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_mlp_i8(tmp)
    print("ALL E2E INT8 TESTS PASS")
