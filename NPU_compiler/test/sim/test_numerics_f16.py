#!/usr/bin/env python3
"""f16 数值语义单测:次正规(subnormal)渐进下溢。

回归背景:f32ToF16Bits 曾对 exp<=0 一律 FTZ(刷成 ±0),导致
silu(gate)·up 这类 ~1e-5 量级的乘积在 DRAM 物化时被整体清零,
softmax 验收曾因容差空洞(atol 远大于信号)带着全零输出「通过」。

本测试直接验证:次正规量级的 add 结果不被清零,且与 numpy f16 一致。
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIC = os.path.join(REPO, "build", "minic")
SPEC = os.path.join(REPO, "configs", "npu256.json")
sys.path.insert(0, os.path.join(REPO, "python"))

import mininpu_rt

MLIR = """
func.func @vadd(%x: tensor<4x64xf16>, %w: tensor<4x64xf16>) -> tensor<4x64xf16> {
  %0 = minigraph.add %x, %w
       : (tensor<4x64xf16>, tensor<4x64xf16>) -> tensor<4x64xf16>
  return %0 : tensor<4x64xf16>
}
"""


def test_f16_subnormal(tmp):
    mlir_path = os.path.join(tmp, "add.mlir")
    with open(mlir_path, "w") as f:
        f.write(MLIR)
    manifest = {"inputs": [{"arg": 0, "name": "x", "npy": "px.npy"},
                           {"arg": 1, "name": "w", "npy": "pw.npy"}]}
    manifest_path = os.path.join(tmp, "w.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f)
    bin_path = os.path.join(tmp, "add.minisa")
    r = subprocess.run([MINIC, "--input", mlir_path, "--emit", bin_path,
                        "--spec", SPEC, "--manifest", manifest_path],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr

    rng = np.random.default_rng(0)
    # 次正规量级:f16 最小正规数 6.1e-5,这里取 1e-6 ~ 4e-5 区间
    x = (rng.standard_normal((4, 64)) * 1e-5).astype(np.float16)
    w = (rng.standard_normal((4, 64)) * 1e-5).astype(np.float16)
    assert (x != 0).any() and (np.abs(x) < 6.1e-5).all(), "构造数据应全在次正规区"

    outs = mininpu_rt.run(bin_path, {"x": x, "w": w}, spec=SPEC)
    out = outs["out0"]
    ref = (x.astype(np.float32) + w.astype(np.float32)).astype(np.float16)

    nz_out = int((out != 0).sum())
    nz_ref = int((ref != 0).sum())
    assert nz_ref > 200, f"numpy 参考非零元 {nz_ref},构造数据失效"
    assert nz_out >= nz_ref * 0.95, \
        f"次正规被清零:NPU 非零元 {nz_out} vs numpy {nz_ref}(疑似 FTZ 回归)"
    np.testing.assert_allclose(out.astype(np.float32), ref.astype(np.float32),
                               atol=2e-7, rtol=1e-2)
    print(f"PASS test_f16_subnormal (nonzero {nz_out}/{nz_ref}, "
          f"max_err={np.abs(out.astype(np.float32) - ref.astype(np.float32)).max():.2e})")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_f16_subnormal(tmp)
    print("ALL NUMERICS TESTS PASS")
