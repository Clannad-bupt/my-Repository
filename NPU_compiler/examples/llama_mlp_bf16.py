#!/usr/bin/env python3
"""LLaMA MLP 的 BF16 示例(README §6.6/§13 步骤 12,三期)。

与 llama_mlp.py 同模块/同 shape(默认 S=32,H=256,I=512)、同 seed 0(权重
即一份),但全图走 bf16:
  - npy 存盘约定:numpy 无 bf16 → 权重/x/gamma 存 f32 npy,minic 常量段与
    minirt 输入装载时按 f32→bf16 RNE 截位(§6.6);设备 dump-out 反向上转
    f32 写盘;
  - 图:rms_norm → matmul(gate, epilogue=silu) → matmul(up) → mul
        → matmul(down),全 bf16 类型,走 OP_GEMM 统一浮点路径
    (C 块 bf16 驻留 SRAM,跨 Tk 部分和 bf16 量化 2⁻⁸,镜像 §2.3 的 f16
    性质;eps imm 按 bf16 位型编码);
  - 黄金 = FP32 参考前向(权重未截位):bf16 尾数 8 bit,单次舍入 2⁻⁹,
    三个 GEMM + 逐元素链路预计 rel-L2 ~1e-2 量级。

用法:
  python3.12 examples/llama_mlp_bf16.py --export /tmp/mlp_bf16
产物:
  mlp_bf16.mlir   minigraph 文本 IR(全 bf16 类型)
  manifest.json   weights(gamma/W, f32 npy → 常量段 bf16)vs inputs(x)
  ref_fp32.npy    FP32 黄金;x_fp32.npy 为运行期输入(装载时转 bf16)
"""
import argparse
import json
import os

import numpy as np

from llama_mlp import LlamaMLP  # §14.1 同一参考模块(同 seed 即同权重)


def _fmt_float(v):
    """MLIR 浮点字面量必须含小数点(1e-06 → 1.0e-06)。"""
    s = repr(float(v))
    if "." not in s and "e" in s:
        s = s.replace("e", ".0e", 1)
    return s


def emit_mlir(args):
    S, H, I = args.seq, args.hidden, args.inter
    T = lambda s: f"tensor<{'x'.join(map(str, s))}xbf16>"
    eps = _fmt_float(1e-6)
    f = []
    f.append("// 三期-BF16(README §6.6):全 bf16 MLP,npy 以 f32 存盘、装载时截位")
    f.append(f"func.func @mlp_bf16("
             f"%x: {T([S,H])}, %gamma: {T([H])}, "
             f"%w_g: {T([H,I])}, %w_u: {T([H,I])}, %w_d: {T([I,H])}"
             f") -> {T([S,H])} {{")
    f.append(f"  %xn = minigraph.rms_norm %x, %gamma {{eps = {eps} : f64}} "
             f": ({T([S,H])}, {T([H])}) -> {T([S,H])}")
    f.append(f"  %g = minigraph.matmul %xn, %w_g {{epilogue = \"silu\"}} "
             f": ({T([S,H])}, {T([H,I])}) -> {T([S,I])}")
    f.append(f"  %u = minigraph.matmul %xn, %w_u "
             f": ({T([S,H])}, {T([H,I])}) -> {T([S,I])}")
    f.append(f"  %h = minigraph.mul %g, %u "
             f": ({T([S,I])}, {T([S,I])}) -> {T([S,I])}")
    f.append(f"  %o = minigraph.matmul %h, %w_d "
             f": ({T([S,I])}, {T([I,H])}) -> {T([S,H])}")
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

    import torch
    torch.manual_seed(0)  # §11.2:同 f16 示例同 seed,保证各格式权重即一份
    os.makedirs(args.export, exist_ok=True)
    model = LlamaMLP(args.hidden, args.inter).eval()
    x32 = torch.randn(args.seq, args.hidden)
    with torch.no_grad():
        ref32 = model(x32)  # FP32 黄金(权重未截位)

    out = args.export
    def dump(name, arr):
        np.save(os.path.join(out, name), arr)
    # npy 约定:bf16 一律以 f32 存盘(§6.6),装载侧(常量/输入)截位
    dump("ref_fp32.npy", ref32.numpy())
    dump("x_fp32.npy", x32.numpy())
    dump("gamma_f32.npy", model.norm_gamma.data.numpy())
    dump("w_gate_f32.npy", model.w_gate.data.numpy())
    dump("w_up_f32.npy", model.w_up.data.numpy())
    dump("w_down_f32.npy", model.w_down.data.numpy())

    mlir_path = os.path.join(out, "mlp_bf16.mlir")
    with open(mlir_path, "w") as f:
        f.write(emit_mlir(args))

    manifest = {
        "inputs": [
            {"arg": 0, "name": "x", "npy": "x_fp32.npy"},
        ],
        "weights": [
            {"arg": 1, "name": "gamma", "npy": "gamma_f32.npy"},
            {"arg": 2, "name": "w_gate", "npy": "w_gate_f32.npy"},
            {"arg": 3, "name": "w_up", "npy": "w_up_f32.npy"},
            {"arg": 4, "name": "w_down", "npy": "w_down_f32.npy"},
        ],
    }
    manifest_path = os.path.join(out, "manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=1)

    print(f"exported to {out}")
    print(f"minigraph: {mlir_path}\nmanifest:  {manifest_path}")


if __name__ == "__main__":
    main()
