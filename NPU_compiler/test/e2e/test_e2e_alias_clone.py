#!/usr/bin/env python3
"""别名回填回归(fx_to_minigraph 词边界修复)。

回归背景:contiguous/clone 视为别名,旧回填是 `%name[, ) 或空格]` 的
字符串替换——matmul/add/mul 的最后一个操作数后面紧跟换行,替换漏命中,
别名喂到行尾操作数时生成引用未定义 SSA 名的非法 MLIR,minic 解析失败。
本用例让 clone 恰好落在 matmul 的 B(行尾)操作数上,走完整 e2e 链。
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIC = os.path.join(REPO, "build", "minic")
SPEC = os.path.join(REPO, "configs", "npu256.json")
sys.path.insert(0, os.path.join(REPO, "python"))

try:
    import torch
    import torch.nn as nn
    import mininpu_rt
    from torch_bridge import export_to_minigraph
except ImportError as e:
    print(f"SKIP: {e}")
    sys.exit(0)


class CloneMM(nn.Module):
    """clone 是 matmul 的行尾 B 操作数(回归触发位)。"""

    def forward(self, x, w):
        return x @ w.clone()


def test_alias_at_line_end(tmp):
    torch.manual_seed(0)
    model = CloneMM().eval()
    x32 = torch.randn(32, 256)
    w32 = torch.randn(256, 512) / 256 ** 0.5
    with torch.no_grad():
        ref = model(x32, w32)

    ep = torch.export.export(model, (x32, w32))
    nodes = list(ep.graph_module.graph.nodes)
    clone_nodes = [n for n in nodes if str(getattr(n, "target", ""))
                                     .startswith("aten.clone")]
    assert clone_nodes, "前置条件:导出图中应含 aten.clone 节点(回归触发源)"
    mlir_path, manifest_path = export_to_minigraph(ep, tmp, name="clone_mm")
    with open(mlir_path) as f:
        mlirs = f.read()
    # 别名回填成功后,clone 被归约到真身 %w,MLIR 不应残留悬空 %clone
    assert "%clone" not in mlirs, \
        f"别名回填残留未定义 SSA 引用(行尾操作数回归?):\n{mlirs}"

    bin_path = os.path.join(tmp, "clone_mm.minisa")
    r = subprocess.run([MINIC, "--input", mlir_path, "--emit", bin_path,
                        "--spec", SPEC, "--manifest", manifest_path],
                       capture_output=True, text=True)
    assert r.returncode == 0, f"minic 解析失败(别名回填回归?):\n{r.stderr}"

    outs = mininpu_rt.run(bin_path, {"x": x32.half().numpy(),
                                     "w": w32.half().numpy()}, spec=SPEC)
    out = outs["out0"]
    assert np.abs(out).sum() > 0.5 * np.abs(ref.numpy()).sum(), \
        "输出量级异常(疑似清零回归)"
    np.testing.assert_allclose(out.astype(np.float32), ref.numpy(),
                               atol=1e-2, rtol=1e-2)
    print("PASS test_alias_at_line_end (clone→matmul 行尾操作数, e2e 一致)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_alias_at_line_end(tmp)
    print("ALL ALIAS TESTS PASS")
