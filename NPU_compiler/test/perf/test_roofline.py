#!/usr/bin/env python3
"""步骤 10 验收:Roofline 报告(README §2 性能口径、§11 性能行、§13 步骤 10)。

对 M=N=K=256 的方阵 MatMul 跑出 Roofline 数字(不设硬门槛):
  - 编译 256³ matmul → minirt 带 timeline 执行
  - scripts/plot_roofline.py 解析 trace,给出 AI/计算上限/带宽上限/实际 TFLOPS
  - 断言报告含**实际 DMA 字节**(trace 事件 args,非理论体积),
    且数值自洽:dma_bytes ≥ 理论最小流量(A+B+C 的 f16 字节数)
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIC = os.path.join(REPO, "build", "minic")
MINIRT = os.path.join(REPO, "build", "minirt")
SPEC = os.path.join(REPO, "configs", "npu256.json")
PLOT = os.path.join(REPO, "scripts", "plot_roofline.py")

sys.path.insert(0, os.path.join(REPO, "scripts"))
import plot_roofline

MLIR = """
func.func @mm(%a: tensor<256x256xf16>, %b: tensor<256x256xf16>) -> tensor<256x256xf16> {
  %0 = minigraph.matmul %a, %b
       : (tensor<256x256xf16>, tensor<256x256xf16>) -> tensor<256x256xf16>
  return %0 : tensor<256x256xf16>
}
"""


def test_roofline_256(tmp):
    import json
    mlir_path = os.path.join(tmp, "mm.mlir")
    with open(mlir_path, "w") as f:
        f.write(MLIR)
    rng = np.random.default_rng(0)
    for i, nm in enumerate(("a", "b")):
        np.save(os.path.join(tmp, f"{nm}.npy"),
                rng.standard_normal((256, 256)).astype(np.float16))
    manifest = {"inputs": [{"arg": 0, "name": "a", "npy": os.path.join(tmp, "a.npy")},
                           {"arg": 1, "name": "b", "npy": os.path.join(tmp, "b.npy")}]}
    mf = os.path.join(tmp, "w.json")
    with open(mf, "w") as f:
        json.dump(manifest, f)
    bin_path = os.path.join(tmp, "mm.minisa")
    r = subprocess.run([MINIC, "--input", mlir_path, "--emit", bin_path,
                        "--spec", SPEC, "--manifest", mf],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr

    trace = os.path.join(tmp, "trace.json")
    r = subprocess.run([MINIRT, "--bin", bin_path, "--spec", SPEC,
                        "--dump-out", os.path.join(tmp, "out.npy"),
                        "--dump-timeline", trace],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr

    report_md = os.path.join(tmp, "roofline.md")
    r = subprocess.run([sys.executable, PLOT, trace, "--spec", SPEC,
                        "--out", report_md],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    print(r.stdout)

    spec = plot_roofline.load_spec(SPEC)
    res = plot_roofline.analyze(trace, spec)

    flops_expected = 2 * 256 ** 3
    assert res["flops"] == flops_expected, \
        f"GEMM FLOPs {res['flops']} != 2·256³={flops_expected}"
    min_traffic = 3 * 256 * 256 * 2  # A+B+C f16 理论最小流量
    assert res["dma_bytes"] >= min_traffic, \
        f"实际 DMA 字节 {res['dma_bytes']} 小于理论最小 {min_traffic},口径错误"
    assert res["total_cycles"] > 0 and res["perf_tflops"] > 0
    assert res["ai"] > 0 and res["roofline_cap"] > 0

    text = open(report_md).read()
    for needle in ("实际 DMA 字节", "算术强度", "TFLOPS", "带宽屋顶", "计算屋顶",
                   "为什么没打满峰值"):
        assert needle in text, f"报告缺「{needle}」"
    print(f"PASS test_roofline_256 "
          f"(AI={res['ai']:.2f}, perf={res['perf_tflops']:.4f} TFLOPS, "
          f"cap={res['roofline_cap']:.4f}, dma={res['dma_bytes']:,}B, "
          f"overlap={res['overlap']:,} cycles)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_roofline_256(tmp)
    print("ALL ROOFLINE TESTS PASS")
