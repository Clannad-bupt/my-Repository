#!/usr/bin/env python3
"""LLaMA MLP 示例：导出 FX Graph、权重与黄金参考（README §14.1、§17）。

用法:
  python3.12 examples/llama_mlp.py --export /tmp/mlp

产物（写入 --export 目录）:
  fx_graph.txt       FX 图文本 + export signature（调试）
  input_fp16.npy     设备侧输入（FP16）
  weight_*.npy       权重（FP16，编译期进 minisa Const 段）
  ref_fp32.npy       PyTorch FP32 参考输出（黄金，见 §11.2 FP16 数值约定）
"""
import argparse
import os
import sys

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F


def rmsnorm(x, gamma, eps):
    ms = x.pow(2).mean(dim=-1, keepdim=True)
    return x * torch.rsqrt(ms + eps) * gamma


class LlamaMLP(nn.Module):
    """README §14.1 的参考模块。x: [S, H]。"""

    def __init__(self, h=256, i=512, eps=1e-6):
        super().__init__()
        self.norm_gamma = nn.Parameter(torch.ones(h))
        self.w_gate = nn.Parameter(torch.randn(h, i) / h**0.5)
        self.w_up = nn.Parameter(torch.randn(h, i) / h**0.5)
        self.w_down = nn.Parameter(torch.randn(i, h) / i**0.5)
        self.eps = eps

    def forward(self, x):
        x = rmsnorm(x, self.norm_gamma, self.eps)
        gate = F.silu(x @ self.w_gate)
        up = x @ self.w_up
        return (gate * up) @ self.w_down


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--export", required=True, help="输出目录")
    ap.add_argument("--seq", type=int, default=32)
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--inter", type=int, default=512)
    args = ap.parse_args()

    torch.manual_seed(0)  # §11.2：禁止无 seed 随机
    os.makedirs(args.export, exist_ok=True)

    model = LlamaMLP(args.hidden, args.inter).eval()  # FP32 参考模型
    x32 = torch.randn(args.seq, args.hidden)
    with torch.no_grad():
        ref32 = model(x32)

    ep = torch.export.export(model, (x32,))

    np.save(os.path.join(args.export, "ref_fp32.npy"), ref32.numpy())
    np.save(os.path.join(args.export, "input_fp16.npy"), x32.half().numpy())
    for name, p in model.named_parameters():
        np.save(os.path.join(args.export, f"weight_{name}.npy"),
                p.detach().half().numpy())

    with open(os.path.join(args.export, "fx_graph.txt"), "w") as f:
        f.write(str(ep.graph_module.graph))
        f.write("\n\n# graph_signature\n")
        f.write(str(ep.graph_signature) + "\n")

    # 同目录再产一份 minigraph MLIR + manifest(§17 演示流的 minic 输入);
    # manifest 里的输入 npy 占位路径在此落盘,使三步 CLI 无需手工补文件
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "..", "python"))
    import json

    from torch_bridge import export_to_minigraph
    mlir_path, manifest_path = export_to_minigraph(ep, args.export, name="mlp")
    with open(manifest_path) as f:
        manifest = json.load(f)
    for inp in manifest["inputs"]:
        p = os.path.join(args.export, inp["npy"])
        os.makedirs(os.path.dirname(p), exist_ok=True)
        np.save(p, x32.half().numpy())
    print(f"exported to {args.export}: {len(ep.graph.nodes)} graph nodes, "
          f"{sum(p.numel() for p in model.parameters())} params")
    print(f"minigraph: {mlir_path}\nmanifest:  {manifest_path}")


if __name__ == "__main__":
    main()
