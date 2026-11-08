#!/usr/bin/env python3
"""端到端(PyTorch,README §13 步骤 2+7 验收):torch.export → torch_bridge → minic → minirt → 黄金对比。

  test_torch_mlp: §14.1 LlamaMLP(torch 2.x export 出的 aten 分解图),
    RMSNorm 6 节点模板由 bridge 匹配(§7.3),权重按 f16 落盘进常量段,
    黄金 = PyTorch FP32 前向(§11.2)。
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
sys.path.insert(0, os.path.join(REPO, "python"))
sys.path.insert(0, os.path.join(REPO, "examples"))

try:
    import torch
    from llama_mlp import LlamaMLP
    from torch_bridge import export_to_minigraph
except ImportError as e:
    print(f"SKIP test_torch_mlp: {e}")
    sys.exit(0)


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0, f"cmd failed: {' '.join(cmd)}\n{r.stderr}"
    return r


def test_torch_mlp(tmp):
    torch.manual_seed(0)  # §11.2:禁止无 seed 随机
    model = LlamaMLP(256, 512).eval()
    x32 = torch.randn(32, 256)
    with torch.no_grad():
        ref32 = model(x32)

    ep = torch.export.export(model, (x32,))
    mlir_path, manifest_path = export_to_minigraph(ep, tmp, name="mlp")
    # bridge 只负责权重;输入 npy 由调用方按 manifest 的名字落盘
    np.save(os.path.join(tmp, "mlp_data", "x.npy"), x32.half().numpy())
    ref_path = os.path.join(tmp, "ref_fp32.npy")
    np.save(ref_path, ref32.numpy())

    text = open(mlir_path).read()
    assert "minigraph.rms_norm" in text, "RMSNorm 6 节点模板应被匹配(§7.3)"
    assert "aten." not in text, "白名单算子应全部翻译完"
    manifest = json.load(open(manifest_path))
    assert len(manifest["weights"]) == 4 and len(manifest["inputs"]) == 1

    bin_path = os.path.join(tmp, "mlp.minisa")
    run([MINIC, "--input", mlir_path, "--emit", bin_path,
         "--spec", SPEC, "--manifest", manifest_path])
    out_path = os.path.join(tmp, "out.npy")
    run([MINIRT, "--bin", bin_path, "--spec", SPEC,
         "--dump-out", out_path,
         "--dump-timeline", os.path.join(tmp, "tl.json")])
    run([sys.executable, COMPARE, out_path, "--ref", ref_path,
         "--atol", "1e-2", "--rtol", "1e-2"])
    print("PASS test_torch_mlp (torch.export → bridge → minisa → sim)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_torch_mlp(tmp)
    print("ALL TORCH E2E TESTS PASS")
