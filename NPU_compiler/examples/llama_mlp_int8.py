#!/usr/bin/env python3
"""LLaMA MLP 的 W8A8 INT8 示例(README §6.5/§13 步骤 11,二期-INT8)。

与 llama_mlp.py 同模块/同 shape(默认 S=32,H=256,I=512),但:
  - 权重按 per-channel(输出列)对称量化到 int8,sb[n] = absmax/127;
  - 激活按 per-token(行)对称量化,sa[m] 由 host 侧对参考前向估计:

      sa_g : 由 host 对「f16 rmsnorm(x)」镜像计算(与设备逐步对齐),
             加 2% headroom 吸收 host/device 最后一个 ulp 差;
      sa_d : 由 FP32 参考前向的 hidden 估计,加 10% headroom;
             越界即 V_QUANT 饱和 §6.5(headroom 内实测无饱和)。

    在线 absmax 单元(取代 host 估计 sa)是下一步扩展(README §19);

  - 图:rms_norm(f16) → quant → quant_matmul(gate) → silu
        → quant_matmul(up) ⊙ → mul → quant → quant_matmul(down)
    全部矩阵乘走 GEMM_I8(i32 精确累加)+ V_DEQUANT 一次性还原。

用法:
  python3.12 examples/llama_mlp_int8.py --export /tmp/mlp_i8
产物:
  mlp_int8.mlir   minigraph 文本 IR(quant/quant_matmul 新 op)
  manifest.json   weights(gamma/W_q/sb,进 minisa Const)vs inputs(x/sa)
  ref_fp32.npy    FP32 黄金;x_fp16.npy / sa_*.npy 为运行期输入
"""
import argparse
import json
import os

import numpy as np
import torch
import torch.nn.functional as F

from llama_mlp import LlamaMLP, rmsnorm  # 复用 §14.1 同一参考模块


def quant_per_channel(w32):
    """对称 per-channel(列)量化:w[K,N] → (i8, sb[N])。"""
    sb = torch.clamp(w32.abs().amax(dim=0) / 127.0, min=1e-12)
    q = torch.clamp(torch.round(w32 / sb), -127, 127)
    return q.to(torch.int8), sb


def _fmt_float(v):
    """MLIR 浮点字面量必须含小数点(1e-06 → 1.0e-06),同 torch_bridge。"""
    s = repr(float(v))
    if "." not in s and "e" in s:
        s = s.replace("e", ".0e", 1)
    return s


def emit_mlir(args):
    S, H, I = args.seq, args.hidden, args.inter
    T = lambda s, d="f16": f"tensor<{'x'.join(map(str, s))}x{d}>"
    eps = _fmt_float(1e-6)
    f = []
    f.append("// 二期-INT8(README §6.5):W8A8 MLP,i8 权重 + per-token 激活 scale")
    f.append(f"func.func @mlp_int8("
             f"%x: {T([S,H])}, %gamma: {T([H])}, %sa_g: {T([S])}, "
             f"%w_g_q: {T([H,I],'i8')}, %sb_g: {T([I])}, "
             f"%w_u_q: {T([H,I],'i8')}, %sb_u: {T([I])}, "
             f"%sa_d: {T([S])}, "
             f"%w_d_q: {T([I,H],'i8')}, %sb_d: {T([H])}"
             f") -> {T([S,H])} {{")
    f.append(f"  %xn = minigraph.rms_norm %x, %gamma {{eps = {eps} : f64}} "
             f": ({T([S,H])}, {T([H])}) -> {T([S,H])}")
    f.append(f"  %a = minigraph.quant %xn, %sa_g "
             f": ({T([S,H])}, {T([S])}) -> {T([S,H],'i8')}")
    f.append(f"  %g = minigraph.quant_matmul %a, %w_g_q, %sa_g, %sb_g "
             f": ({T([S,H],'i8')}, {T([H,I],'i8')}, {T([S])}, {T([I])}) "
             f"-> {T([S,I])}")
    f.append(f"  %gu = minigraph.silu %g : {T([S,I])} -> {T([S,I])}")
    f.append(f"  %u = minigraph.quant_matmul %a, %w_u_q, %sa_g, %sb_u "
             f": ({T([S,H],'i8')}, {T([H,I],'i8')}, {T([S])}, {T([I])}) "
             f"-> {T([S,I])}")
    f.append(f"  %h = minigraph.mul %gu, %u "
             f": ({T([S,I])}, {T([S,I])}) -> {T([S,I])}")
    f.append(f"  %hq = minigraph.quant %h, %sa_d "
             f": ({T([S,I])}, {T([S])}) -> {T([S,I],'i8')}")
    f.append(f"  %o = minigraph.quant_matmul %hq, %w_d_q, %sa_d, %sb_d "
             f": ({T([S,I],'i8')}, {T([I,H],'i8')}, {T([S])}, {T([H])}) "
             f"-> {T([S,H])}")
    f.append(f"  return %o : {T([S,H])}")
    f.append("}")
    return "\n".join(f) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--export", required=True)
    ap.add_argument("--seq", type=int, default=32)
    ap.add_argument("--hidden", type=int, default=256)
    ap.add_argument("--inter", type=int, default=512)
    args = ap.parse_args()

    torch.manual_seed(0)  # §11.2:同 f16 示例同 seed,保证两版权重即一份
    os.makedirs(args.export, exist_ok=True)
    model = LlamaMLP(args.hidden, args.inter).eval()
    x32 = torch.randn(args.seq, args.hidden)
    with torch.no_grad():
        ref32 = model(x32)
        # ---- host 侧量化(§6.5 约定)----
        w_g_q, sb_g = quant_per_channel(model.w_gate.data)
        w_u_q, sb_u = quant_per_channel(model.w_up.data)
        w_d_q, sb_d = quant_per_channel(model.w_down.data)
        # sa_g:f16 rmsnorm 镜像(与设备 execVecRms 逐步对齐)+ 2% headroom
        xh = x32.half()
        g16 = model.norm_gamma.data.half()
        ms = xh.float().pow(2).mean(dim=-1, keepdim=True)
        xn16 = (xh.float() * torch.rsqrt(ms + model.eps) * g16.float()).half()
        sa_g = xn16.float().abs().amax(dim=1) / 127.0 * 1.02
        # sa_d:FP32 参考前向的 hidden + 10% headroom
        h32 = F.silu(xn16.float() @ model.w_gate.data) \
            * (xn16.float() @ model.w_up.data)
        sa_d = h32.abs().amax(dim=1) / 127.0 * 1.10

    out = args.export
    def dump(name, arr):
        np.save(os.path.join(out, name), arr)
    dump("ref_fp32.npy", ref32.numpy())
    dump("x_fp16.npy", x32.half().numpy())
    dump("sa_g.npy", sa_g.half().numpy())
    dump("sa_d.npy", sa_d.half().numpy())
    dump("gamma_f16.npy", model.norm_gamma.data.half().numpy())
    dump("w_gate_q.npy", w_g_q.numpy())
    dump("sb_g.npy", sb_g.half().numpy())
    dump("w_up_q.npy", w_u_q.numpy())
    dump("sb_u.npy", sb_u.half().numpy())
    dump("w_down_q.npy", w_d_q.numpy())
    dump("sb_d.npy", sb_d.half().numpy())

    mlir_path = os.path.join(out, "mlp_int8.mlir")
    with open(mlir_path, "w") as f:
        f.write(emit_mlir(args))

    manifest = {
        "inputs": [
            {"arg": 0, "name": "x", "npy": "x_fp16.npy"},
            {"arg": 2, "name": "sa_g", "npy": "sa_g.npy"},
            {"arg": 7, "name": "sa_d", "npy": "sa_d.npy"},
        ],
        "weights": [
            {"arg": 1, "name": "gamma", "npy": "gamma_f16.npy"},
            {"arg": 3, "name": "w_gate_q", "npy": "w_gate_q.npy"},
            {"arg": 4, "name": "sb_g", "npy": "sb_g.npy"},
            {"arg": 5, "name": "w_up_q", "npy": "w_up_q.npy"},
            {"arg": 6, "name": "sb_u", "npy": "sb_u.npy"},
            {"arg": 8, "name": "w_down_q", "npy": "w_down_q.npy"},
            {"arg": 9, "name": "sb_d", "npy": "sb_d.npy"},
        ],
    }
    manifest_path = os.path.join(out, "manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=1)

    print(f"exported to {out}")
    print(f"minigraph: {mlir_path}\nmanifest:  {manifest_path}")


if __name__ == "__main__":
    main()
