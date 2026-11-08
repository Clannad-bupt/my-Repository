#!/usr/bin/env python3
"""端到端(README §13 步骤 7 验收):手写 minigraph MLIR → minic --emit → minirt → numpy。

  test_mlp: §14.1 LLaMA MLP,S=32 H=256 I=512(全 16 倍数)。
    rms_norm → matmul(w_gate, transpose_b, epilogue silu) → matmul(w_up)
             → mul → matmul(w_down, transpose_b)
    覆盖:fusion(matmul+silu)、rms_norm/eltwise lowering、
          transpose_b 的 DRAM strided view + meta views 运行时回填、
          常量段打包(manifest weights f32→f16)、逐算子 DRAM 物化。
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIC = os.path.join(REPO, "build", "minic")
MINIRT = os.path.join(REPO, "build", "minirt")
SPEC = os.path.join(REPO, "configs", "npu256.json")
COMPARE = os.path.join(REPO, "scripts", "compare_golden.py")

S, H, I = 32, 256, 512
EPS = 1e-6

MLIR = f"""
func.func @mlp(%x: tensor<{S}x{H}xf16>, %gamma: tensor<{H}xf16>,
               %wg: tensor<{I}x{H}xf16>, %wu: tensor<{H}x{I}xf16>,
               %wd: tensor<{H}x{I}xf16>) -> tensor<{S}x{H}xf16> {{
  %0 = minigraph.rms_norm %x, %gamma {{eps = 1.0e-6 : f64}}
       : (tensor<{S}x{H}xf16>, tensor<{H}xf16>) -> tensor<{S}x{H}xf16>
  %1 = minigraph.matmul %0, %wg {{transpose_b = true}}
       : (tensor<{S}x{H}xf16>, tensor<{I}x{H}xf16>) -> tensor<{S}x{I}xf16>
  %g = minigraph.silu %1 : tensor<{S}x{I}xf16> -> tensor<{S}x{I}xf16>
  %2 = minigraph.matmul %0, %wu
       : (tensor<{S}x{H}xf16>, tensor<{H}x{I}xf16>) -> tensor<{S}x{I}xf16>
  %3 = minigraph.mul %g, %2
       : (tensor<{S}x{I}xf16>, tensor<{S}x{I}xf16>) -> tensor<{S}x{I}xf16>
  %4 = minigraph.matmul %3, %wd {{transpose_b = true}}
       : (tensor<{S}x{I}xf16>, tensor<{H}x{I}xf16>) -> tensor<{S}x{H}xf16>
  return %4 : tensor<{S}x{H}xf16>
}}
"""


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0, f"cmd failed: {' '.join(cmd)}\n{r.stderr}"
    return r


def test_mlp(tmp):
    rng = np.random.default_rng(7)
    # 权重按 f32 存盘(manifest 路径,minic 转 f16 打包进常量段);
    # 黄金参考用 f16 量化后的权重做 f32 算术,与片上数值对齐(§2.3)。
    x16 = rng.standard_normal((S, H)).astype(np.float16)
    gamma32 = np.ones(H, dtype=np.float32)
    wg32 = (rng.standard_normal((I, H)) / H**0.5).astype(np.float32)  # Linear 布局 [I,H]
    wu32 = (rng.standard_normal((H, I)) / H**0.5).astype(np.float32)
    wd32 = (rng.standard_normal((H, I)) / I**0.5).astype(np.float32)

    npy = {"x": x16, "gamma": gamma32, "w_gate": wg32, "w_up": wu32, "w_down": wd32}
    for name, arr in npy.items():
        np.save(os.path.join(tmp, f"{name}.npy"), arr)

    mlir_path = os.path.join(tmp, "mlp.mlir")
    with open(mlir_path, "w") as f:
        f.write(MLIR)
    manifest = {
        "inputs": [{"arg": 0, "name": "x", "npy": "x.npy"}],
        "weights": [
            {"arg": 1, "name": "gamma", "npy": "gamma.npy"},
            {"arg": 2, "name": "w_gate", "npy": "w_gate.npy"},
            {"arg": 3, "name": "w_up", "npy": "w_up.npy"},
            {"arg": 4, "name": "w_down", "npy": "w_down.npy"},
        ],
    }
    manifest_path = os.path.join(tmp, "mlp.weights.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f)

    # 黄金参考(FP32 master,§14.1 公式;权重先量化到 f16 再转回 f32)
    xf = x16.astype(np.float32)
    gamma = gamma32.astype(np.float16).astype(np.float32)
    wg = wg32.astype(np.float16).astype(np.float32)
    wu = wu32.astype(np.float16).astype(np.float32)
    wd = wd32.astype(np.float16).astype(np.float32)
    rms = xf / np.sqrt((xf**2).mean(-1, keepdims=True) + EPS) * gamma
    gate = rms @ wg.T
    gate = gate / (1.0 + np.exp(-gate))  # silu
    up = rms @ wu
    golden = (gate * up) @ wd.T
    golden_path = os.path.join(tmp, "golden.npy")
    np.save(golden_path, golden.astype(np.float32))

    # 编译 → 模拟 → 对比
    bin_path = os.path.join(tmp, "mlp.minisa")
    run([MINIC, "--input", mlir_path, "--emit", bin_path,
         "--spec", SPEC, "--manifest", manifest_path])
    for ext in (".meta.json", ".report.md", ".mininpu.mlir"):
        assert os.path.exists(bin_path[:-7] + ext), f"缺产物 {ext}"

    with open(bin_path[:-7] + ".meta.json") as f:
        meta = json.load(f)
    assert meta["consts"] and len(meta["consts"]) == 4, "4 个权重应进常量段"
    assert meta["views"], "transpose_b 应产生 DRAM strided 视图(§9.6)"
    with open(bin_path[:-7] + ".report.md") as f:
        assert "tile" in f.read().lower(), "report.md 必须含 tile 决策(§9.2)"

    out_path = os.path.join(tmp, "out.npy")
    r = run([MINIRT, "--bin", bin_path, "--spec", SPEC,
             "--dump-out", out_path,
             "--dump-timeline", os.path.join(tmp, "tl.json")])
    assert "total_cycles" in r.stderr

    run([sys.executable, COMPARE, out_path, "--ref", golden_path,
         "--atol", "1e-2", "--rtol", "1e-2"])
    print("PASS test_mlp (e2e: mlir → minisa → sim → numpy)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_mlp(tmp)
    print("ALL E2E TESTS PASS")
