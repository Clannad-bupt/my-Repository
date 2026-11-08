#!/usr/bin/env python3
"""端到端(README §13 步骤 12,三期-BF16 验收):examples/llama_mlp_bf16.py → minic → minirt。

  图:rms_norm → matmul(gate, epilogue=silu) → matmul(up) → mul
      → matmul(down),全 bf16,同 §14.1 shape(S=32,H=256,I=512)。
  覆盖:bf16 verifier/ODS、npy f32→bf16 存盘约定(常量段 + 运行期输入)、
        OP_GEMM 统一浮点路径(A=B=C=bf16)、V_RMS 的 eps imm bf16 位型、
        dump-out 的 bf16→f32 上转。

  数值断言:vs torch FP32 黄金(权重未截位):cos ≥ 0.999、rel-l2 ≤ 1e-2。
    说明:bf16 尾数 8 bit(单次舍入 2⁻⁹),权重/激活/跨 Tk 部分和三级截位
    链式积累;numpy 纯 bf16 语义参考同量级,实测 6.5e-3——与 f16 全链路
    (4.4e-3)同数量级、略宽(尾数少 3 bit),符合 §2.3/§6.6 预测。
  性能断言:ΣGEMM cycles ≤ 1.05 × Σmacs/128 —— bf16 与 f16 同峰
    (128 macs/cyc,§3),实测 ratio=1.000。
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
EXAMPLE = os.path.join(REPO, "examples", "llama_mlp_bf16.py")

S, H, I = 32, 256, 512


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0, f"cmd failed: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}"
    return r


def test_mlp_bf16(tmp):
    exp = os.path.join(tmp, "mlp_bf16")
    run([sys.executable, EXAMPLE, "--export", exp])
    for f in ("mlp_bf16.mlir", "manifest.json", "ref_fp32.npy",
              "x_fp32.npy", "w_gate_f32.npy"):
        assert os.path.exists(os.path.join(exp, f)), f"缺导出产物 {f}"

    bin_path = os.path.join(exp, "mlp_bf16.minisa")
    run([MINIC, "--input", os.path.join(exp, "mlp_bf16.mlir"),
         "--emit", bin_path, "--spec", SPEC,
         "--manifest", os.path.join(exp, "manifest.json")])

    with open(bin_path[:-7] + ".meta.json") as f:
        meta = json.load(f)
    # 4 个权重(gamma + 3×W)进常量段(f32 npy → bf16 截位);x 为运行期输入
    assert len(meta["consts"]) == 4, f"consts={len(meta['consts'])}"

    out_path = os.path.join(tmp, "out_bf16.npy")
    tl_path = os.path.join(tmp, "tl_bf16.json")
    run([MINIRT, "--bin", bin_path, "--spec", SPEC,
         "--dump-out", out_path, "--dump-timeline", tl_path])

    run([sys.executable, COMPARE, out_path,
         "--ref", os.path.join(exp, "ref_fp32.npy"),
         "--cos", "0.999", "--rel-l2", "1e-2"])

    # --- 性能:bf16 与 f16 同峰(128 macs/cyc)---
    with open(tl_path) as f:
        ev = json.load(f)["traceEvents"]
    gemms = [e for e in ev if e["name"] == "GEMM"]
    assert gemms, "timeline 必须含 GEMM(bf16 统一浮点路径)"
    macs = sum(e["args"]["flops"] for e in gemms) // 2
    cyc = sum(e["dur"] for e in gemms)
    ratio = cyc / (macs / 128)
    assert ratio <= 1.05, f"bf16 GEMM 偏离 f16 峰值模型:ratio={ratio:.3f}"
    exp_macs = 2 * S * H * I + S * I * H  # gate + up + down
    assert macs == exp_macs, f"macs={macs} ≠ {exp_macs}"
    print(f"  GEMM(bf16): {cyc} cyc ({macs} macs, "
          f"{macs / cyc:.0f} macs/cyc) = f16 峰值模型 ×{ratio:.3f}")
    print("PASS test_mlp_bf16 (e2e: bf16 mlir → minisa → sim → numpy)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_mlp_bf16(tmp)
    print("ALL E2E BF16 TESTS PASS")
