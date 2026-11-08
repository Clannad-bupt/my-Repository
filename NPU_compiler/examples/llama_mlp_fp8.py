#!/usr/bin/env python3
"""LLaMA MLP 的 FP8(E4M3)W8A8 示例(README §6.6/§13 步骤 12,三期)。

与 llama_mlp_int8.py 同模块/同 shape(默认 S=32,H=256,I=512)、同 seed 0,
但 8-bit 容器换 f8E4M3FN、数值语义相反(对数步长/相对精度):
  - 权重 per-tensor(len-1 sb)量化到 e4m3:sb = absmax(W)/448(无 headroom,
    权重静态精确);位型以 |u1 npy 存盘(npy 永不表达 f8 语义,§6.6);
  - 激活 per-token(行)sa,host 侧对参考前向估计(同 §6.5 模式,域改 448):
      sa_g : f16 rmsnorm 镜像 + 2% headroom;
      sa_d : FP32 参考前向 hidden + 10% headroom。
    e4m3 为对数步长:headroom 只平移指数、几乎不损精度;越界饱和是
    ±448 的软钳位(有界),与 int8 的灾难性饱和性质不同;
  - 图:rms_norm(f16) → quant(f8) → quant_matmul(gate) → silu
        → quant_matmul(up) ⊙ → mul → quant(f8) → quant_matmul(down)
    全部矩阵乘走 GEMM_F8(A/B=e4m3、C=f32 累加——e4m3×e4m3 乘积在 f32
    精确可表示,累加误差仅 f32 RNE,H100 WGMMA 口径)+ V_DEQUANT 一次还原。

Gaussian 权重上 FP8 劣于 INT8 per-channel 是预期且如实入表(单测层的
离群值交叉才是 FP8 的正确卖点,README §6.6)。

用法:
  python3.12 examples/llama_mlp_fp8.py --export /tmp/mlp_fp8
产物:
  mlp_fp8.mlir   minigraph 文本 IR(f8E4M3FN 容器 + len-1 per-tensor sb)
  manifest.json  weights(gamma/sb/w_q)vs inputs(x/sa)
  ref_fp32.npy   FP32 黄金(权重未量化);x_fp16/sa_*.npy 为运行期输入
"""
import argparse
import json
import os

import numpy as np
import torch
import torch.nn.functional as F

from llama_mlp import LlamaMLP  # §14.1 同一参考模块(同 seed 即同权重)


def quant_per_tensor(w32):
    """per-tensor 对称量化:w → (e4m3 位, sb 标量)。sb = absmax/448(§6.6)。"""
    sb = torch.clamp(w32.abs().amax() / 448.0, min=1e-30)
    q = (w32 / sb).to(torch.float8_e4m3fn)  # RNE + 饱和 ±448,与 sim 位级一致
    return q.view(torch.uint8), sb


def _fmt_float(v):
    """MLIR 浮点字面量必须含小数点(1e-06 → 1.0e-06)。"""
    s = repr(float(v))
    if "." not in s and "e" in s:
        s = s.replace("e", ".0e", 1)
    return s


def emit_mlir(args):
    S, H, I = args.seq, args.hidden, args.inter
    T = lambda s, d="f16": f"tensor<{'x'.join(map(str, s))}x{d}>"
    F8 = lambda s: T(s, "f8E4M3FN")
    eps = _fmt_float(1e-6)
    f = []
    f.append("// 三期-FP8(README §6.6):W8A8 MLP,f8E4M3FN 容器 + per-tensor sb(len-1)")
    f.append(f"func.func @mlp_fp8("
             f"%x: {T([S,H])}, %gamma: {T([H])}, %sa_g: {T([S])}, "
             f"%w_g_q: {F8([H,I])}, %sb_g: {T([1])}, "
             f"%w_u_q: {F8([H,I])}, %sb_u: {T([1])}, "
             f"%sa_d: {T([S])}, "
             f"%w_d_q: {F8([I,H])}, %sb_d: {T([1])}"
             f") -> {T([S,H])} {{")
    f.append(f"  %xn = minigraph.rms_norm %x, %gamma {{eps = {eps} : f64}} "
             f": ({T([S,H])}, {T([H])}) -> {T([S,H])}")
    f.append(f"  %a = minigraph.quant %xn, %sa_g "
             f": ({T([S,H])}, {T([S])}) -> {F8([S,H])}")
    f.append(f"  %g = minigraph.quant_matmul %a, %w_g_q, %sa_g, %sb_g "
             f": ({F8([S,H])}, {F8([H,I])}, {T([S])}, {T([1])}) -> {T([S,I])}")
    f.append(f"  %gu = minigraph.silu %g : {T([S,I])} -> {T([S,I])}")
    f.append(f"  %u = minigraph.quant_matmul %a, %w_u_q, %sa_g, %sb_u "
             f": ({F8([S,H])}, {F8([H,I])}, {T([S])}, {T([1])}) -> {T([S,I])}")
    f.append(f"  %h = minigraph.mul %gu, %u "
             f": ({T([S,I])}, {T([S,I])}) -> {T([S,I])}")
    f.append(f"  %hq = minigraph.quant %h, %sa_d "
             f": ({T([S,I])}, {T([S])}) -> {F8([S,I])}")
    f.append(f"  %o = minigraph.quant_matmul %hq, %w_d_q, %sa_d, %sb_d "
             f": ({F8([S,I])}, {F8([I,H])}, {T([S])}, {T([1])}) -> {T([S,H])}")
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

    torch.manual_seed(0)  # §11.2:同 f16 示例同 seed,保证各格式权重即一份
    os.makedirs(args.export, exist_ok=True)
    model = LlamaMLP(args.hidden, args.inter).eval()
    x32 = torch.randn(args.seq, args.hidden)
    with torch.no_grad():
        ref32 = model(x32)  # FP32 黄金(权重未量化)
        # ---- host 侧量化(§6.6 约定:权重 per-tensor,激活 per-token)----
        w_g_q, sb_g = quant_per_tensor(model.w_gate.data)
        w_u_q, sb_u = quant_per_tensor(model.w_up.data)
        w_d_q, sb_d = quant_per_tensor(model.w_down.data)
        # sa_g:f16 rmsnorm 镜像(与设备 execVecRms 逐步对齐)+ 2% headroom
        # (对数步长下 headroom 只平移指数,几乎不损精度)
        xh = x32.half()
        g16 = model.norm_gamma.data.half()
        ms = xh.float().pow(2).mean(dim=-1, keepdim=True)
        xn16 = (xh.float() * torch.rsqrt(ms + model.eps) * g16.float()).half()
        sa_g = xn16.float().abs().amax(dim=1) / 448.0 * 1.02
        # sa_d:FP32 参考前向的 hidden + 10% headroom
        h32 = F.silu(xn16.float() @ model.w_gate.data) \
            * (xn16.float() @ model.w_up.data)
        sa_d = h32.abs().amax(dim=1) / 448.0 * 1.10

    out = args.export
    def dump(name, arr):
        np.save(os.path.join(out, name), arr)
    dump("ref_fp32.npy", ref32.numpy())
    dump("x_fp16.npy", x32.half().numpy())
    dump("sa_g.npy", sa_g.half().numpy())
    dump("sa_d.npy", sa_d.half().numpy())
    dump("gamma_f16.npy", model.norm_gamma.data.half().numpy())
    dump("w_gate_q.npy", w_g_q.numpy())   # |u1:e4m3 位型(§6.6 存盘约定)
    dump("sb_g.npy", sb_g.half().numpy().reshape(1))  # len-1 per-tensor
    dump("w_up_q.npy", w_u_q.numpy())
    dump("sb_u.npy", sb_u.half().numpy().reshape(1))
    dump("w_down_q.npy", w_d_q.numpy())
    dump("sb_d.npy", sb_d.half().numpy().reshape(1))

    mlir_path = os.path.join(out, "mlp_fp8.mlir")
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
