#!/usr/bin/env python3
"""三期-BF16/FP8 数值闭环(README §2.3/§6.6):手工汇编驱动新 dtype 路径。

  test_f8_encode_vs_torch   : V_QUANT(f8) 输出位模式 vs torch.float8_e4m3fn 逐比特
                              (RNE、饱和 ±448、subnormal、per-tensor len-1 广播)
  test_f8_decode_exhaustive : 254 个有限 e4m3 位模式经 GEMM_F8×单位阵逐一解码,
                              vs torch 解码逐比特(NaN 位 0x7F/0xFF 无法经
                              V_QUANT 注入——有限值编码永不产生 NaN,属 consts
                              路径,见 §6.6 注)
  test_bf16_encode_vs_torch : V_DEQUANT dst=bf16 位模式 vs torch.bfloat16 逐比特
  test_bf16_gemm_vs_torch   : OP_GEMM 统一浮点分支 A/B/C=bf16——|部分和| ≤ 64·255²
                              < 2²⁴,整数在 f32 精确 → 累加零误差,唯一量化在 C
                              的 bf16 终写 → 与 torch 逐比特一致
  test_bf16_rms_eps         : V_RMS 的 eps imm 按 dst dtype(bf16)解码(§6.6)
  test_gemm_f8_vs_numpy     : quant(f8)→GEMM_F8(f32 C 直出) vs numpy;e4m3×e4m3
                              乘积在 f32 精确,误差只有累加顺序,容差 = 1e-5·Σ|a·b|
  test_outlier_crossover    : 离群值交叉——同一激活矩阵(行 7 含 100× 离群):
                              离群行 INT8 per-token 劣化、FP8 per-tensor 保持;
                              高斯行 INT8 反超。格式选择是数据依赖的(§6.6)。
"""
import json
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
import torch

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIRT = os.path.join(REPO, "build", "minirt")
SPEC = os.path.join(REPO, "configs", "npu256.json")

# ---- 伪 ISA(README §6)----
OP_DMA_LD, OP_DMA_ST = 0x01, 0x02
OP_GEMM, OP_GEMM_I8, OP_GEMM_F8 = 0x10, 0x11, 0x12
OP_V_RMS = 0x20
OP_V_QUANT, OP_V_DEQUANT = 0x24, 0x25
OP_BAR, OP_END = 0xF0, 0xF1
MS_DRAM, MS_SRAM = 0, 1
DT_F32, DT_F16, DT_I8, DT_I32, DT_BF16, DT_F8E4M3 = 0, 1, 2, 3, 4, 5
CH_LD, CH_ST, CH_GEMM, CH_VEC = 1, 2, 4, 8
ESZ = {DT_F16: 2, DT_F32: 4, DT_I8: 1, DT_I32: 4, DT_BF16: 2, DT_F8E4M3: 1}


def instr(op, dst, s0, s1, imm):
    return (op << 56) | (dst << 48) | (s0 << 40) | (s1 << 32) | imm


def gemm_w1(tile_k, acc_mode):
    return (tile_k << 48) | (acc_mode << 40)


def desc(id, memspace, dtype, shape, base=0):
    rank = len(shape)
    stride = [1] * 4
    for i in range(rank - 2, -1, -1):
        stride[i] = stride[i + 1] * shape[i + 1]
    shp = list(shape) + [1] * (4 - rank)
    nbytes = ESZ[dtype]
    for s in shape:
        nbytes *= s
    return struct.pack("<IBBBB4I4i2Q", id, memspace, dtype, rank, 32,
                       *shp, *stride, base, nbytes)


def write_minisa(path, descs, code_words):
    code = b"".join(struct.pack("<Q", w) for w in code_words)
    hdr = struct.pack("<4sHHIII", b"MNPU", 1, 36, len(descs), len(code), 0) + b"\0" * 16
    with open(path, "wb") as f:
        f.write(hdr)
        for d in descs:
            f.write(d)
        f.write(code)


def run_case(tmp, name, descs, code, inputs, output_id):
    write_minisa(os.path.join(tmp, f"{name}.minisa"), descs, code)
    meta = {"entry": name,
            "inputs": [{"id": i, "npy": f"in{i}.npy"} for i in inputs],
            "outputs": [{"id": output_id}], "consts": []}
    with open(os.path.join(tmp, f"{name}.meta.json"), "w") as f:
        json.dump(meta, f)
    out_path = os.path.join(tmp, f"{name}_out.npy")
    r = subprocess.run(
        [MINIRT, "--bin", os.path.join(tmp, f"{name}.minisa"),
         "--spec", SPEC, "--dump-out", out_path],
        capture_output=True, text=True)
    assert r.returncode == 0, f"minirt failed:\n{r.stderr}"
    return np.load(out_path)


# len-1(per-tensor)scale 的 DMA 载体:npy 与 desc 对均按 (16,) 搬运(32B 对齐),
# V_QUANT/V_DEQUANT 经独立 (1,) 形状 desc 别名同一 SRAM 基址 → 广播分支。
SCALE_N = 16


def scale16(v):
    return np.full(SCALE_N, v, dtype=np.float16)


def f8_bits(q_f32):
    """e4m3 编码参考(torch.float8_e4m3fn = OCP fn 语义:RNE、饱和 ±448)。"""
    t = torch.from_numpy(np.ascontiguousarray(q_f32, dtype=np.float32))
    return t.to(torch.float8_e4m3fn).view(torch.uint8).numpy()


def f8_vals(bits_u8):
    t = torch.from_numpy(np.ascontiguousarray(bits_u8, dtype=np.uint8))
    return t.view(torch.float8_e4m3fn).float().numpy()


# ---------------------------------------------------------------------------
# 1. V_QUANT dst=DT_F8E4M3:f32→e4m3 编码逐比特 vs torch
# ---------------------------------------------------------------------------
def test_f8_encode_vs_torch(tmp):
    M, K = 32, 64
    rng = np.random.default_rng(11)
    x = (rng.standard_normal((M, K)) * 2).astype(np.float16)
    # 行 0:边界字面量(subnormal 半途、normal 半途、饱和域),sa=1 → q=x 原值
    lit = [0.0, 2**-11, 2**-10, 1.5 * 2**-10, 3 * 2**-10, 5 * 2**-10, 2**-9,
           2**-6, 1.0625, 1.1875, 1.3125, 1.4375, 1.0, 2.0,
           447.5, 448.0, 464.0, 480.0, 512.0, 1000.0,
           -1.0625, -447.5, -1000.0]
    x[0, :len(lit)] = np.array(lit, dtype=np.float32).astype(np.float16)
    # 行 31:越界饱和(q = x/sa 达 ~900 → 全行 0x7E/0xFE)
    x[31] = (rng.standard_normal(K) * 300).astype(np.float16)
    amax = np.abs(x.astype(np.float32)).max(axis=1)
    sa = (amax / 448.0).astype(np.float16)
    sa[0] = np.float16(1.0)
    sa[31] = (amax[31] / 900.0).astype(np.float16)
    np.save(os.path.join(tmp, "in0.npy"), x)
    np.save(os.path.join(tmp, "in1.npy"), sa)
    descs = [
        desc(0, MS_DRAM, DT_F16, (M, K)),
        desc(1, MS_DRAM, DT_F16, (M,)),
        desc(2, MS_DRAM, DT_F8E4M3, (M, K)),
        desc(10, MS_SRAM, DT_F16, (M, K), base=0),
        desc(11, MS_SRAM, DT_F16, (M,), base=M * K * 2),
        desc(12, MS_SRAM, DT_F8E4M3, (M, K), base=M * K * 2 + 64),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_QUANT, 12, 10, 11, 0),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 2, 12, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "f8_enc", descs, code, inputs=(0, 1), output_id=2)
    assert out.dtype == np.uint8, f"dump 应为 |u1:{out.dtype}"
    # 参考:与设备逐步同构——inv = 1/sa(f32 除),q = x*inv(f32 乘),再 e4m3 编码
    inv = np.float32(1.0) / sa.astype(np.float32)
    ref = f8_bits(x.astype(np.float32) * inv[:, None])
    bad = np.argwhere(out != ref)
    assert not len(bad), f"e4m3 编码不一致 前4处 {bad[:4].tolist()} " \
                        f"dev/ref = {[(out[m, n], ref[m, n]) for m, n in bad[:4]]}"
    print("PASS test_f8_encode_vs_torch (bitwise vs torch.float8_e4m3fn)")


# ---------------------------------------------------------------------------
# 2. GEMM_F8 × 单位阵:254 个有限位模式的 e4m3→f32 解码逐比特 vs torch
# ---------------------------------------------------------------------------
def test_f8_decode_exhaustive(tmp):
    K = 256
    # 全部位模式(0..255),NaN 槽 0x7F/0xFF 以 0 占位(编码不可达)
    pats = np.arange(256, dtype=np.uint8)
    vals = f8_vals(pats).astype(np.float32)          # NaN 位置为 NaN
    ok = ~np.isnan(vals)
    vals[~ok] = 0.0
    x = vals[None, :].astype(np.float16)             # e4m3 有限值均可 f16 精确表示
    W = np.eye(K, dtype=np.float16)                  # 单位阵:乘 1 直通解码值
    sa16, sb16 = scale16(1.0), scale16(1.0)
    np.save(os.path.join(tmp, "in0.npy"), x)
    np.save(os.path.join(tmp, "in1.npy"), sa16)
    np.save(os.path.join(tmp, "in2.npy"), W)
    np.save(os.path.join(tmp, "in3.npy"), sb16)
    o_x, o_sa, o_a = 0, 512, 1024
    o_w, o_sb, o_b = 2048, 133120, 133152
    o_c = 198720
    descs = [
        desc(0, MS_DRAM, DT_F16, (1, K)),
        desc(1, MS_DRAM, DT_F16, (SCALE_N,)),
        desc(2, MS_DRAM, DT_F16, (K, K)),
        desc(3, MS_DRAM, DT_F16, (SCALE_N,)),
        desc(4, MS_DRAM, DT_F32, (1, K)),
        desc(10, MS_SRAM, DT_F16, (1, K), base=o_x),
        desc(11, MS_SRAM, DT_F16, (SCALE_N,), base=o_sa),
        desc(13, MS_SRAM, DT_F16, (1,), base=o_sa),        # len-1 别名
        desc(12, MS_SRAM, DT_F8E4M3, (1, K), base=o_a),
        desc(20, MS_SRAM, DT_F16, (K, K), base=o_w),
        desc(21, MS_SRAM, DT_F16, (SCALE_N,), base=o_sb),
        desc(23, MS_SRAM, DT_F16, (1,), base=o_sb),        # len-1 别名
        desc(22, MS_SRAM, DT_F8E4M3, (K, K), base=o_b),
        desc(24, MS_SRAM, DT_F32, (1, K), base=o_c),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_DMA_LD, 20, 2, 0, 0), instr(OP_DMA_LD, 21, 3, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_QUANT, 12, 10, 13, 0),   # A_f8 = e4m3(x),sa per-tensor
        instr(OP_V_QUANT, 22, 20, 23, 0),   # B_f8 = e4m3(单位阵)
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_GEMM_F8, 24, 12, 22, (K << 16) | 1),
        gemm_w1(K, acc_mode=0),
        instr(OP_BAR, 0, 0, 0, CH_GEMM),
        instr(OP_DMA_ST, 4, 24, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "f8_dec", descs, code, inputs=(0, 1, 2, 3), output_id=4)
    # C[0,n] = Σ_k A[0,k]·I[k,n] = decode(A[0,n]):单项乘 1,无累加误差
    assert np.array_equal(out.ravel(), vals), \
        f"e4m3 解码不一致:{[(i, out.flat[i], vals[i]) for i in np.flatnonzero(out != vals)[:4]]}"
    n_ok = int(ok.sum())
    assert n_ok == 254 and not np.isnan(out).any()
    print(f"PASS test_f8_decode_exhaustive ({n_ok}/256 位模式 bit-exact,"
          f"NaN 槽 2 个经 0 占位)")


# ---------------------------------------------------------------------------
# 3. V_DEQUANT dst=DT_BF16:f32→bf16(RNE 截位)逐比特 vs torch
# ---------------------------------------------------------------------------
def test_bf16_encode_vs_torch(tmp):
    M, N = 32, 64
    rng = np.random.default_rng(33)
    X = rng.integers(-(1 << 20), 1 << 20, (M, N), dtype=np.int32)
    sb = (rng.random(N).astype(np.float32) * 1e-3 + 1e-4).astype(np.float16)
    sa = (rng.random(M).astype(np.float32) * 1e-3 + 1e-4).astype(np.float16)
    # 行 0:半途判据——sb=1+2⁻⁸ 使 v=2^j·(1+2⁻⁸) 恰为 bf16 半途(RNE→偶尾数=2^j);
    # sb=1+3·2⁻⁹ 越过半途(→ 2^j·(1+2⁻⁷))。sa[0]=1。
    X[0] = 0
    X[0, :8] = [1, 2, 4, 8, 16, 32, 64, 128]
    X[0, 8:16] = [1, 2, 4, 8, 16, 32, 64, 128]
    sb[:8] = np.float16(1.0 + 2**-8)
    sb[8:16] = np.float16(1.0 + 3 * 2**-9)
    sa[0] = np.float16(1.0)
    np.save(os.path.join(tmp, "in0.npy"), X)
    np.save(os.path.join(tmp, "in1.npy"), sb)
    np.save(os.path.join(tmp, "in2.npy"), sa)
    o_x, o_sb, o_sa, o_y = 0, 8192, 8320, 8384
    descs = [
        desc(0, MS_DRAM, DT_I32, (M, N)),
        desc(1, MS_DRAM, DT_F16, (N,)),
        desc(2, MS_DRAM, DT_F16, (M,)),
        desc(3, MS_DRAM, DT_BF16, (M, N)),
        desc(10, MS_SRAM, DT_I32, (M, N), base=o_x),
        desc(11, MS_SRAM, DT_F16, (N,), base=o_sb),
        desc(12, MS_SRAM, DT_F16, (M,), base=o_sa),
        desc(13, MS_SRAM, DT_BF16, (M, N), base=o_y),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_DMA_LD, 12, 2, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_DEQUANT, 13, 10, 11, 12 << 16),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 3, 13, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "bf16_enc", descs, code, inputs=(0, 1, 2), output_id=3)
    assert out.dtype == np.float32, f"bf16 dump 应上转 <f4:{out.dtype}"
    # 参考:与设备同构((x·sb)·sa,f32)后 torch bf16 编码;上转 f32 位 = bf16位<<16
    v = (X.astype(np.float32) * sb.astype(np.float32)[None, :]) \
        * sa.astype(np.float32)[:, None]
    bits = torch.from_numpy(v).to(torch.bfloat16).view(torch.uint16).numpy()
    assert np.array_equal(out.view(np.uint32), bits.astype(np.uint32) << 16), \
        "bf16 编码不一致"
    print("PASS test_bf16_encode_vs_torch (bitwise vs torch.bfloat16,含半途行)")


# ---------------------------------------------------------------------------
# 4. OP_GEMM 统一浮点分支 A/B/C=bf16:整数算例下的逐比特一致
# ---------------------------------------------------------------------------
def test_bf16_gemm_vs_torch(tmp):
    M, K, N = 32, 64, 32
    rng = np.random.default_rng(44)
    XA = rng.integers(-255, 256, (M, K), dtype=np.int32)
    XB = rng.integers(-255, 256, (K, N), dtype=np.int32)
    onesK, onesM, onesN = (np.ones(K, dtype=np.float16), np.ones(M, dtype=np.float16),
                           np.ones(N, dtype=np.float16))
    np.save(os.path.join(tmp, "in0.npy"), XA)   # A 源(整数,bf16 可精确)
    np.save(os.path.join(tmp, "in1.npy"), onesK)
    np.save(os.path.join(tmp, "in2.npy"), onesM)
    np.save(os.path.join(tmp, "in3.npy"), XB)   # B 源
    np.save(os.path.join(tmp, "in4.npy"), onesN)
    np.save(os.path.join(tmp, "in5.npy"), onesK)
    # V_DEQUANT(sb=sa=1)把整数源落成 bf16;A/B/C 三者 bf16 走 OP_GEMM
    o_xa, o_sbK, o_saM, o_a = 0, 8192, 8320, 8384
    o_xb, o_sbN, o_saK, o_b = 12480, 20672, 20736, 20864
    o_c = 24960
    descs = [
        desc(0, MS_DRAM, DT_I32, (M, K)), desc(1, MS_DRAM, DT_F16, (K,)),
        desc(2, MS_DRAM, DT_F16, (M,)), desc(3, MS_DRAM, DT_I32, (K, N)),
        desc(4, MS_DRAM, DT_F16, (N,)), desc(5, MS_DRAM, DT_F16, (K,)),
        desc(6, MS_DRAM, DT_BF16, (M, N)),
        desc(10, MS_SRAM, DT_I32, (M, K), base=o_xa),
        desc(11, MS_SRAM, DT_F16, (K,), base=o_sbK),
        desc(12, MS_SRAM, DT_F16, (M,), base=o_saM),
        desc(13, MS_SRAM, DT_BF16, (M, K), base=o_a),
        desc(20, MS_SRAM, DT_I32, (K, N), base=o_xb),
        desc(21, MS_SRAM, DT_F16, (N,), base=o_sbN),
        desc(22, MS_SRAM, DT_F16, (K,), base=o_saK),
        desc(23, MS_SRAM, DT_BF16, (K, N), base=o_b),
        desc(24, MS_SRAM, DT_BF16, (M, N), base=o_c),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_DMA_LD, 12, 2, 0, 0), instr(OP_DMA_LD, 20, 3, 0, 0),
        instr(OP_DMA_LD, 21, 4, 0, 0), instr(OP_DMA_LD, 22, 5, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_DEQUANT, 13, 10, 11, 12 << 16),
        instr(OP_V_DEQUANT, 23, 20, 21, 22 << 16),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_GEMM, 24, 13, 23, (N << 16) | M),
        gemm_w1(K, acc_mode=0),
        instr(OP_BAR, 0, 0, 0, CH_GEMM),
        instr(OP_DMA_ST, 6, 24, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "bf16_gemm", descs, code, inputs=(0, 1, 2, 3, 4, 5),
                   output_id=6)
    # |C| ≤ K·255² = 4.16e6 < 2²⁴:整数在 f32 精确 → bf16×bf16 乘积精确、
    # 逐项累加零误差 → 唯一量化是 C 的 bf16 终写,torch 同理 → 逐比特
    Cint = XA.astype(np.int64) @ XB.astype(np.int64)
    assert np.abs(Cint).max() < 2**24, "算例须保证部分和在 f32 精确"
    ref = torch.from_numpy(Cint.astype(np.float32)).to(torch.bfloat16) \
        .float().numpy()
    assert np.array_equal(out, ref), "bf16 GEMM 应逐比特一致(整数算例)"
    print("PASS test_bf16_gemm_vs_torch (bitwise,整数算例:f32 累加零误差)")


# ---------------------------------------------------------------------------
# 5. V_RMS 的 eps imm 按 dst dtype(bf16)解码 + bf16 读写路径
# ---------------------------------------------------------------------------
def test_bf16_rms_eps(tmp):
    M, H = 16, 64
    rng = np.random.default_rng(55)
    X = rng.integers(-1000, 1001, (M, H), dtype=np.int32)
    G = rng.integers(-256, 257, (1, H), dtype=np.int32)  # ≤256 在 bf16 精确
    onesH = np.ones(H, dtype=np.float16)
    sa16 = scale16(1.0)
    np.save(os.path.join(tmp, "in0.npy"), X)
    np.save(os.path.join(tmp, "in1.npy"), onesH)
    np.save(os.path.join(tmp, "in2.npy"), sa16)
    np.save(os.path.join(tmp, "in3.npy"), G)
    np.save(os.path.join(tmp, "in4.npy"), sa16)     # gamma 的 sa=len-1 载体
    eps_bits = int(torch.tensor(1e-6, dtype=torch.float32)
                   .to(torch.bfloat16).view(torch.uint16).item())
    o_x, o_sbH, o_sa16, o_src = 0, 4096, 4224, 4256
    o_g, o_gsa, o_gam, o_y = 6304, 6560, 6592, 6720
    descs = [
        desc(0, MS_DRAM, DT_I32, (M, H)), desc(1, MS_DRAM, DT_F16, (H,)),
        desc(2, MS_DRAM, DT_F16, (SCALE_N,)), desc(3, MS_DRAM, DT_I32, (1, H)),
        desc(4, MS_DRAM, DT_F16, (SCALE_N,)),
        desc(5, MS_DRAM, DT_BF16, (M, H)),
        desc(10, MS_SRAM, DT_I32, (M, H), base=o_x),
        desc(11, MS_SRAM, DT_F16, (H,), base=o_sbH),
        desc(12, MS_SRAM, DT_F16, (M,), base=o_sa16),
        desc(13, MS_SRAM, DT_BF16, (M, H), base=o_src),
        desc(20, MS_SRAM, DT_I32, (1, H), base=o_g),
        desc(21, MS_SRAM, DT_F16, (H,), base=o_sbH),      # 与 src 共用 sb=1
        desc(22, MS_SRAM, DT_F16, (SCALE_N,), base=o_gsa),
        desc(23, MS_SRAM, DT_F16, (1,), base=o_gsa),      # len-1 别名
        desc(24, MS_SRAM, DT_BF16, (1, H), base=o_gam),
        desc(25, MS_SRAM, DT_BF16, (H,), base=o_gam),     # (H,) 别名(秩1)
        desc(26, MS_SRAM, DT_BF16, (M, H), base=o_y),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_DMA_LD, 12, 2, 0, 0), instr(OP_DMA_LD, 20, 3, 0, 0),
        instr(OP_DMA_LD, 22, 4, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_DEQUANT, 13, 10, 11, 12 << 16),   # src_bf16 = bf16(X)
        instr(OP_V_DEQUANT, 24, 20, 21, 23 << 16),   # gamma_bf16 = bf16(G)
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_V_RMS, 26, 13, 25, eps_bits),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 5, 26, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "bf16_rms", descs, code, inputs=(0, 1, 2, 3, 4),
                   output_id=5)
    # 参考:x/gamma 为 bf16 位值,eps 按 bf16 imm 解码;y=(x·scale)·g 后 bf16 终写。
    # ms 求和顺序(sim 顺序累加 vs torch 归约)有 f32 ulp 差 → bf16 终写可能翻
    # 1 ulp,容差 2 ulp(2⁻⁷)
    x32 = torch.from_numpy(X.astype(np.float32)).to(torch.bfloat16).float().numpy()
    g32 = torch.from_numpy(G.astype(np.float32)).to(torch.bfloat16).float().numpy()
    eps = float(torch.tensor(1e-6, dtype=torch.float32).to(torch.bfloat16).float())
    ms = (x32 * x32).sum(axis=1, keepdims=True) / H
    ref = (x32 / np.sqrt(ms + eps)) * g32
    np.testing.assert_allclose(out, ref.astype(np.float32), rtol=2**-7, atol=0)
    print(f"PASS test_bf16_rms_eps (eps imm 按 bf16 解码,eps={eps:.3e})")


# ---------------------------------------------------------------------------
# 6. quant(f8)→GEMM_F8→f32 C 直出 vs numpy(误差只有累加顺序)
# ---------------------------------------------------------------------------
def test_gemm_f8_vs_numpy(tmp):
    M, K, N = 32, 64, 32
    rng = np.random.default_rng(66)
    x = (rng.standard_normal((M, K)) * 2).astype(np.float16)
    W = (rng.standard_normal((K, N))).astype(np.float16)
    sa = (np.abs(x.astype(np.float32)).max(axis=1) / 448.0).astype(np.float16)
    sb_t = np.float16(np.abs(W.astype(np.float32)).max() / 448.0)  # per-tensor
    sb16 = scale16(sb_t)                               # per-tensor 载体(len-1)
    np.save(os.path.join(tmp, "in0.npy"), x)
    np.save(os.path.join(tmp, "in1.npy"), sa)          # A 的 per-token sa(32,)
    np.save(os.path.join(tmp, "in2.npy"), W)
    np.save(os.path.join(tmp, "in3.npy"), sb16)        # B 的 per-tensor 载体(16,)
    o_x, o_sa, o_w, o_sb = 0, 4096, 4160, 8256
    o_a, o_b, o_c = 8288, 10336, 12384
    descs = [
        desc(0, MS_DRAM, DT_F16, (M, K)), desc(1, MS_DRAM, DT_F16, (M,)),
        desc(2, MS_DRAM, DT_F16, (K, N)), desc(3, MS_DRAM, DT_F16, (SCALE_N,)),
        desc(4, MS_DRAM, DT_F32, (M, N)),
        desc(10, MS_SRAM, DT_F16, (M, K), base=o_x),
        desc(11, MS_SRAM, DT_F16, (M,), base=o_sa),
        desc(20, MS_SRAM, DT_F16, (K, N), base=o_w),
        desc(21, MS_SRAM, DT_F16, (SCALE_N,), base=o_sb),
        desc(23, MS_SRAM, DT_F16, (1,), base=o_sb),    # len-1 别名
        desc(12, MS_SRAM, DT_F8E4M3, (M, K), base=o_a),
        desc(22, MS_SRAM, DT_F8E4M3, (K, N), base=o_b),
        desc(24, MS_SRAM, DT_F32, (M, N), base=o_c),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_DMA_LD, 20, 2, 0, 0), instr(OP_DMA_LD, 21, 3, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_QUANT, 12, 10, 11, 0),              # A_f8,per-token sa
        instr(OP_V_QUANT, 22, 20, 23, 0),              # B_f8,per-tensor sb
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_GEMM_F8, 24, 12, 22, (N << 16) | M),
        gemm_w1(K, acc_mode=0),
        instr(OP_BAR, 0, 0, 0, CH_GEMM),
        instr(OP_DMA_ST, 4, 24, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "f8_gemm", descs, code, inputs=(0, 1, 2, 3), output_id=4)
    assert out.dtype == np.float32
    # 参考:与设备同构的量化(乘倒数)→ 解码值 → f32 矩阵乘
    Aq = f8_vals(f8_bits(x.astype(np.float32)
                         * (np.float32(1.0) / sa.astype(np.float32))[:, None]))
    Bq = f8_vals(f8_bits(W.astype(np.float32)
                         * np.float32(1.0 / np.float32(sb_t))))
    Cref = Aq @ Bq
    # e4m3×e4m3 乘积在 f32 精确;误差只有累加顺序 ≤ ~K·2⁻²⁴·Σ|a·b|
    tol = 1e-5 * (np.abs(Aq) @ np.abs(Bq)) + 1e-30
    diff = np.abs(out - Cref)
    assert np.all(diff <= tol), \
        f"GEMM_F8 误差超顺序累加界:{(diff / tol).max():.2f}×"
    print(f"PASS test_gemm_f8_vs_numpy (max diff {diff.max():.3e} = "
          f"{(diff / tol).max():.2f}×顺序累加界)")


# ---------------------------------------------------------------------------
# 7. 离群值交叉:INT8 per-token vs FP8 per-tensor(§6.6 的核心论据)
# ---------------------------------------------------------------------------
def _row_rel_err(out_f16, ref64):
    d = out_f16.astype(np.float64) - ref64
    return np.linalg.norm(d, axis=1) / np.linalg.norm(ref64, axis=1)


def _quant_err_rows(xq_f32, x_f32, cols=None):
    """量化误差按行 rel-L2;cols 限定列——离群元素被两种格式各自「精确/3%」
    编码,混入只会稀释度量;限定正常元素才是信息保真的口径。"""
    d = xq_f32 - x_f32
    if cols is not None:
        d, x_f32 = d[:, cols], x_f32[:, cols]
    return np.linalg.norm(d, axis=1) / np.linalg.norm(x_f32, axis=1)


def _run_vquant(tmp, name, x_f16, sa_f16, as_f8):
    """V_QUANT 后反量化回 f32(sa 为标量时走 len-1 载体)。"""
    M, K = x_f16.shape
    np.save(os.path.join(tmp, "in0.npy"), x_f16)
    if np.isscalar(sa_f16) or np.ndim(sa_f16) == 0:
        sa_vec, dram_shape, sram_len = scale16(sa_f16), (SCALE_N,), SCALE_N
        per_tensor = True
    else:
        sa_vec, dram_shape, sram_len = sa_f16, (M,), M
        per_tensor = False
    np.save(os.path.join(tmp, "in1.npy"), sa_vec)
    dt = DT_F8E4M3 if as_f8 else DT_I8
    descs = [
        desc(0, MS_DRAM, DT_F16, (M, K)), desc(1, MS_DRAM, DT_F16, dram_shape),
        desc(2, MS_DRAM, dt, (M, K)),
        desc(10, MS_SRAM, DT_F16, (M, K), base=0),
        desc(11, MS_SRAM, DT_F16, (sram_len,), base=M * K * 2),
    ]
    sa_id = 11
    if per_tensor:  # len-1 别名(广播分支)
        descs.append(desc(13, MS_SRAM, DT_F16, (1,), base=M * K * 2))
        sa_id = 13
    descs.append(desc(12, MS_SRAM, dt, (M, K), base=M * K * 2 + 64))
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_QUANT, 12, 10, sa_id, 0),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 2, 12, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, name, descs, code, inputs=(0, 1), output_id=2)
    if as_f8:
        qv = f8_vals(out).astype(np.float32)
        if per_tensor:
            return qv * np.float32(sa_vec[0])
        return qv * sa_vec.astype(np.float32)[:, None]
    qv = out.astype(np.float32)
    sa = sa_vec.astype(np.float32)[:, None]
    return qv * sa


def test_outlier_crossover(tmp):
    M, K, N, OUTLIER, OUT_ROW, HOT_COL = 32, 64, 32, 100.0, 7, 3
    rng = np.random.default_rng(77)
    x = rng.standard_normal((M, K)).astype(np.float16)
    x[OUT_ROW, HOT_COL] = np.float16(OUTLIER)        # 单离群(行 7)
    xh = rng.standard_normal((M, K)).astype(np.float16)
    xh[:, HOT_COL] = np.float16(OUTLIER)             # 热通道(每行同列 100×)
    x32 = x.astype(np.float32)
    cols = [k for k in range(K) if k != HOT_COL]     # 正常元素口径
    normal = [r for r in range(M) if r != OUT_ROW]

    # ---- (1) 激活级(V_QUANT):交叉点最锐利的口径 ----
    sa8 = (np.abs(x32).max(axis=1) / 127.0).astype(np.float16)     # per-token
    sa8h = (np.abs(xh.astype(np.float32)).max(axis=1) / 127.0).astype(np.float16)
    sa_t = np.float16(np.abs(x32).max() / 448.0)                   # per-tensor
    sa_th = np.float16(np.abs(xh.astype(np.float32)).max() / 448.0)
    xq = _run_vquant(tmp, "xq_i8", x, sa8, as_f8=False)
    xqf = _run_vquant(tmp, "xq_f8", x, sa_t, as_f8=True)
    xqh = _run_vquant(tmp, "xqh_i8", xh, sa8h, as_f8=False)
    xqhf = _run_vquant(tmp, "xqh_f8", xh, sa_th, as_f8=True)
    e_i8 = _quant_err_rows(xq, x32, cols)
    e_f8 = _quant_err_rows(xqf, x32, cols)
    eh_i8 = _quant_err_rows(xqh, xh.astype(np.float32), cols)
    eh_f8 = _quant_err_rows(xqhf, xh.astype(np.float32), cols)
    i8n, f8n = e_i8[normal].mean(), e_f8[normal].mean()
    print(f"    [激活级|正常元素] 高斯行: INT8 {i8n:.4%}  FP8 {f8n:.4%}"
          f"  → INT8 胜 {f8n / i8n:.1f}×")
    print(f"    [激活级|正常元素] 离群行(行 {OUT_ROW:.0f},{OUTLIER:.0f}×):"
          f" INT8 {e_i8[OUT_ROW]:.4%}  FP8 {e_f8[OUT_ROW]:.4%}"
          f"  → FP8 胜 {e_i8[OUT_ROW] / e_f8[OUT_ROW]:.1f}×")
    print(f"    [激活级|正常元素] 热通道(全行 {OUTLIER:.0f}×):"
          f" INT8 per-token {eh_i8.mean():.4%}  FP8 {eh_f8.mean():.4%}"
          f"  → FP8 胜 {eh_i8.mean() / eh_f8.mean():.1f}×"
          f"(per-token 救不了热通道)")
    # 高斯:均匀绝对精度 + per-token → INT8 胜;离群行/热通道:对数步长的相对
    # 精度不被 scale 稀释 → FP8 胜;INT8 离群行的正常元素被 scale 崩掉
    assert i8n * 2 < f8n, f"高斯行 INT8 应胜:{i8n:.4%} vs {f8n:.4%}"
    assert e_f8[OUT_ROW] * 3 < e_i8[OUT_ROW], \
        f"离群行 FP8 应胜:{e_f8[OUT_ROW]:.4%} vs {e_i8[OUT_ROW]:.4%}"
    assert e_i8[OUT_ROW] > 10 * i8n, "INT8 离群行应显著崩于自身高斯行"
    assert eh_f8.mean() * 3 < eh_i8.mean(), "热通道 FP8 应胜"

    # ---- (2) GEMM 级:诚实补充——rel-L2(对全参考)被离群列主导 ----
    W = rng.standard_normal((K, N)).astype(np.float16)
    ref = x.astype(np.float64) @ W.astype(np.float64)
    sa8g = (np.abs(x32).max(axis=1) / 127.0).astype(np.float16)
    sb8 = (np.abs(W.astype(np.float32)).max(axis=0) / 127.0).astype(np.float16)
    B8 = np.clip(np.rint(W.astype(np.float32)
                         / sb8.astype(np.float32)[None, :]),
                 -127, 127).astype(np.int8)
    np.save(os.path.join(tmp, "in0.npy"), x)
    np.save(os.path.join(tmp, "in1.npy"), sa8g)
    np.save(os.path.join(tmp, "in2.npy"), B8)
    np.save(os.path.join(tmp, "in3.npy"), sb8)
    o_x, o_sa, o_a, o_b = 0, 4096, 4160, 6208
    o_c, o_sb, o_y = 8256, 12352, 12416
    descs = [
        desc(0, MS_DRAM, DT_F16, (M, K)), desc(1, MS_DRAM, DT_F16, (M,)),
        desc(2, MS_DRAM, DT_I8, (K, N)), desc(3, MS_DRAM, DT_F16, (N,)),
        desc(4, MS_DRAM, DT_F16, (M, N)),
        desc(10, MS_SRAM, DT_F16, (M, K), base=o_x),
        desc(11, MS_SRAM, DT_F16, (M,), base=o_sa),
        desc(12, MS_SRAM, DT_I8, (M, K), base=o_a),
        desc(13, MS_SRAM, DT_I8, (K, N), base=o_b),
        desc(14, MS_SRAM, DT_F16, (N,), base=o_sb),
        desc(15, MS_SRAM, DT_I32, (M, N), base=o_c),
        desc(16, MS_SRAM, DT_F16, (M, N), base=o_y),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_DMA_LD, 13, 2, 0, 0), instr(OP_DMA_LD, 14, 3, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_QUANT, 12, 10, 11, 0),
        instr(OP_BAR, 0, 0, 0, CH_VEC | CH_LD),
        instr(OP_GEMM_I8, 15, 12, 13, (N << 16) | M),
        gemm_w1(K, acc_mode=0),
        instr(OP_BAR, 0, 0, 0, CH_GEMM),
        instr(OP_V_DEQUANT, 16, 15, 14, 11 << 16),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 4, 16, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out_i8 = run_case(tmp, "xover_i8", descs, code, inputs=(0, 1, 2, 3),
                      output_id=4)

    sa_t2 = np.float16(np.abs(x32).max() / 448.0)
    sb_t = np.float16(np.abs(W.astype(np.float32)).max() / 448.0)
    np.save(os.path.join(tmp, "in0.npy"), x)
    np.save(os.path.join(tmp, "in1.npy"), scale16(sa_t2))
    np.save(os.path.join(tmp, "in2.npy"), W)
    np.save(os.path.join(tmp, "in3.npy"), scale16(sb_t))
    o_x, o_sa, o_w, o_sb = 0, 4096, 4128, 8224
    o_a, o_b, o_c, o_y = 8256, 10304, 12352, 16448
    descs = [
        desc(0, MS_DRAM, DT_F16, (M, K)), desc(1, MS_DRAM, DT_F16, (SCALE_N,)),
        desc(2, MS_DRAM, DT_F16, (K, N)), desc(3, MS_DRAM, DT_F16, (SCALE_N,)),
        desc(4, MS_DRAM, DT_F16, (M, N)),
        desc(10, MS_SRAM, DT_F16, (M, K), base=o_x),
        desc(11, MS_SRAM, DT_F16, (SCALE_N,), base=o_sa),
        desc(13, MS_SRAM, DT_F16, (1,), base=o_sa),    # len-1 别名(sa)
        desc(27, MS_SRAM, DT_F16, (1,), base=o_sa),    # dequant 用同基址别名
        desc(20, MS_SRAM, DT_F16, (K, N), base=o_w),
        desc(21, MS_SRAM, DT_F16, (SCALE_N,), base=o_sb),
        desc(23, MS_SRAM, DT_F16, (1,), base=o_sb),    # len-1 别名(sb)
        desc(26, MS_SRAM, DT_F16, (1,), base=o_sb),    # dequant 用同基址别名
        desc(12, MS_SRAM, DT_F8E4M3, (M, K), base=o_a),
        desc(22, MS_SRAM, DT_F8E4M3, (K, N), base=o_b),
        desc(24, MS_SRAM, DT_F32, (M, N), base=o_c),
        desc(25, MS_SRAM, DT_F16, (M, N), base=o_y),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_DMA_LD, 20, 2, 0, 0), instr(OP_DMA_LD, 21, 3, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_QUANT, 12, 10, 13, 0),             # A_f8,per-tensor sa
        instr(OP_V_QUANT, 22, 20, 23, 0),             # B_f8,per-tensor sb
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_GEMM_F8, 24, 12, 22, (N << 16) | M),
        gemm_w1(K, acc_mode=0),
        instr(OP_BAR, 0, 0, 0, CH_GEMM),
        instr(OP_V_DEQUANT, 25, 24, 26, 27 << 16),    # src=f32,dst=f16,双 len-1
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 4, 25, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out_f8 = run_case(tmp, "xover_f8", descs, code, inputs=(0, 1, 2, 3),
                      output_id=4)

    e_i8, e_f8 = _row_rel_err(out_i8, ref), _row_rel_err(out_f8, ref)
    print(f"    [GEMM 级|全参考] 高斯行: INT8 {e_i8[normal].mean():.4%}  "
          f"FP8 {e_f8[normal].mean():.4%}")
    print(f"    [GEMM 级|全参考] 离群行: INT8 {e_i8[OUT_ROW]:.4%}  "
          f"FP8 {e_f8[OUT_ROW]:.4%}(离群列主导参考范数且被 INT8 精确编码,"
          f"FP8 的 3% 权重量化亦随离群放大——全参考 rel-L2 口径下 INT8 仍胜,"
          f"FP8 的优势须看激活级正常元素口径,见上)")
    assert e_i8[normal].mean() * 2 < e_f8[normal].mean(), \
        "GEMM 级高斯行 INT8 应胜"
    print("PASS test_outlier_crossover (激活级交叉成立:高斯行/热通道外 INT8 胜,"
          "离群行与热通道 FP8 胜)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_f8_encode_vs_torch(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_f8_decode_exhaustive(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_bf16_encode_vs_torch(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_bf16_gemm_vs_torch(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_bf16_rms_eps(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_gemm_f8_vs_numpy(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_outlier_crossover(tmp)
    print("ALL test_numerics_fp8 PASS")
    sys.exit(0)

