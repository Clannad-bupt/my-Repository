#!/usr/bin/env python3
"""步骤 8 验收:Python 侧 runtime.run(bin, inputs) -> numpy(README §13)。

  test_runtime_run: 手写 add 图经 minic 编译后,用 mininpu_rt.run 喂 numpy 输入,
                    输出对上 numpy;timeline 里应能看到 HOST_CPY 事件(§10/§11.3)。
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
func.func @vadd(%x: tensor<32x256xf16>, %w: tensor<32x256xf16>) -> tensor<32x256xf16> {
  %0 = minigraph.add %x, %w
       : (tensor<32x256xf16>, tensor<32x256xf16>) -> tensor<32x256xf16>
  return %0 : tensor<32x256xf16>
}
"""


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0, f"cmd failed: {' '.join(cmd)}\n{r.stderr}"
    return r


def test_runtime_run(tmp):
    mlir_path = os.path.join(tmp, "add.mlir")
    with open(mlir_path, "w") as f:
        f.write(MLIR)
    manifest = {"inputs": [{"arg": 0, "name": "x", "npy": "unused_x.npy"},
                           {"arg": 1, "name": "w", "npy": "unused_w.npy"}]}
    # manifest 仅用于给输入命名;npy 路径在 run() 时被替换,填占位即可
    manifest_path = os.path.join(tmp, "add.weights.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f)
    bin_path = os.path.join(tmp, "add.minisa")
    run([MINIC, "--input", mlir_path, "--emit", bin_path,
         "--spec", SPEC, "--manifest", manifest_path])

    rng = np.random.default_rng(3)
    x = rng.standard_normal((32, 256)).astype(np.float16)
    w = rng.standard_normal((32, 256)).astype(np.float16)
    tl_path = os.path.join(tmp, "tl.json")
    outs = mininpu_rt.run(bin_path, {"x": x, "w": w}, spec=SPEC, timeline=tl_path)

    assert set(outs) == {"out0"}, f"输出名异常: {set(outs)}"
    ref = x.astype(np.float32) + w.astype(np.float32)
    np.testing.assert_allclose(outs["out0"].astype(np.float32), ref,
                               atol=1e-3, rtol=1e-2)

    evs = json.load(open(tl_path))["traceEvents"]
    assert any(e.get("pid") == "host" or e.get("cat") == "host"
               for e in evs), "timeline 缺 HOST_CPY(host)事件(§10/§11.3)"
    print("PASS test_runtime_run (runtime.run → numpy)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_runtime_run(tmp)
    print("ALL RUNTIME-API TESTS PASS")
