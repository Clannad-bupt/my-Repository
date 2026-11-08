#!/usr/bin/env python3
"""描述符去重回归(CodeGen §6.1 修复)。

回归背景:tile 循环 unroll 后每次 DMA 各带一个 view op,CodeGen 原为
每个 view op 建独立描述符 → 512³ GEMM 产生 ~2600 个描述符,直接撞
§6.1 的 256 上限报错「描述符数量超过 256」。修复为几何相同的视图共享
描述符(memspace/parent/offset/shape/stride 五元组),相同负载降到 13 个。

本测试断言:512³ matmul 可编译且描述符数远小于 256、数值正确。
"""
import json
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIC = os.path.join(REPO, "build", "minic")
MINIRT = os.path.join(REPO, "build", "minirt")
SPEC = os.path.join(REPO, "configs", "npu256.json")

MLIR = """
func.func @mm(%a: tensor<512x512xf16>, %b: tensor<512x512xf16>) -> tensor<512x512xf16> {
  %0 = minigraph.matmul %a, %b
       : (tensor<512x512xf16>, tensor<512x512xf16>) -> tensor<512x512xf16>
  return %0 : tensor<512x512xf16>
}
"""


def test_descriptor_dedup_512(tmp):
    mlir_path = os.path.join(tmp, "mm.mlir")
    with open(mlir_path, "w") as f:
        f.write(MLIR)
    rng = np.random.default_rng(0)
    for i, nm in enumerate(("a", "b")):
        np.save(os.path.join(tmp, f"{nm}.npy"),
                rng.standard_normal((512, 512)).astype(np.float16))
    manifest = {"inputs": [{"arg": 0, "name": "a", "npy": os.path.join(tmp, "a.npy")},
                           {"arg": 1, "name": "b", "npy": os.path.join(tmp, "b.npy")}]}
    mf = os.path.join(tmp, "w.json")
    with open(mf, "w") as f:
        json.dump(manifest, f)
    bin_path = os.path.join(tmp, "mm.minisa")
    r = subprocess.run([MINIC, "--input", mlir_path, "--emit", bin_path,
                        "--spec", SPEC, "--manifest", mf],
                       capture_output=True, text=True)
    assert r.returncode == 0, \
        f"512³ 编译失败(描述符去重回归?):\n{r.stderr}"

    # bin 头部: magic(4) version(2) hdrsize(2) DescCount(4)
    with open(bin_path, "rb") as f:
        f.seek(8)
        (ndesc,) = struct.unpack("<I", f.read(4))
    assert ndesc <= 64, f"描述符 {ndesc} 个,去重失效(修复前 512³ 为 ~2600)"

    r = subprocess.run([MINIRT, "--bin", bin_path, "--spec", SPEC,
                        "--dump-out", os.path.join(tmp, "out.npy"),
                        "--dump-timeline", os.path.join(tmp, "tl.json")],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr

    out = np.load(os.path.join(tmp, "out.npy"))
    a = np.load(os.path.join(tmp, "a.npy")).astype(np.float64)
    b = np.load(os.path.join(tmp, "b.npy")).astype(np.float64)
    ref = a @ b  # f64 参考;out 为 f16、k 段间每段一次 f16 回写(~8 段 × ulp)
    err = np.abs(out.astype(np.float64) - ref)
    rel = err.max() / np.abs(ref).max()
    assert rel < 1e-2, f"相对误差 {rel:.2e} 超容差"
    assert np.abs(out.astype(np.float64)).sum() > 0.5 * np.abs(ref).sum(), \
        "输出量级异常(疑似清零回归)"
    print(f"PASS test_descriptor_dedup_512 "
          f"(desc={ndesc}, max_rel_err={rel:.2e})")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_descriptor_dedup_512(tmp)
    print("ALL DESC SCALE TESTS PASS")
