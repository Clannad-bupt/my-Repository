#!/usr/bin/env python3
"""二期-INT8 数值闭环(README §2.3/§6.5):手工汇编 V_QUANT / GEMM_I8 / V_DEQUANT。

  test_quant_device_vs_numpy : V_QUANT 与 numpy RNE+饱和逐比特一致
  test_gemm_i8_exact         : GEMM_I8 与 numpy int32 精确相等(无浮点参与)
  test_gemm_i8_acc_tiles     : acc_mode 跨 Tk tile 的 i32 精确累加
  test_dequant_device_vs_numpy: V_DEQUANT(sa×sb)与 numpy fp32 参考逐比特一致
  test_worst_case_bound      : 127²×K 上界算例无溢出(精确到个位)
  test_quant_gemm_dequant_chain: 三指令闭环对 numpy 参考(容差 f16 终量化)
"""
import json
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIRT = os.path.join(REPO, "build", "minirt")
SPEC = os.path.join(REPO, "configs", "npu256.json")

# ---- 伪 ISA(README §6)----
OP_DMA_LD, OP_DMA_ST = 0x01, 0x02
OP_GEMM, OP_GEMM_I8 = 0x10, 0x11
OP_V_QUANT, OP_V_DEQUANT = 0x24, 0x25
OP_BAR, OP_END = 0xF0, 0xF1
MS_DRAM, MS_SRAM = 0, 1
DT_F32, DT_F16, DT_I8, DT_I32 = 0, 1, 2, 3
CH_LD, CH_ST, CH_GEMM, CH_VEC = 1, 2, 4, 8
ESZ = {DT_F16: 2, DT_F32: 4, DT_I8: 1, DT_I32: 4}


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


# 与 lib/sim/Sim.cpp::execVecQuant 逐步对齐的参考实现
def ref_quant(x_f16, sa_f16):
    x = x_f16.astype(np.float32)
    inv = np.float32(1.0) / sa_f16.astype(np.float32)
    q = np.rint(x * inv[:, None])
    return np.clip(q, -127, 127).astype(np.int8)


def make_quant_case(tmp, M, K, seed=0):
    rng = np.random.default_rng(seed)
    x = (rng.standard_normal((M, K)) * 2).astype(np.float16)
    sa = (np.abs(x.astype(np.float32)).max(axis=1) / 127.0).astype(np.float16)
    np.save(os.path.join(tmp, "in0.npy"), x)
    np.save(os.path.join(tmp, "in1.npy"), sa)
    xB, saB, yB = M * K * 2, 0, 0  # sram 槽位偏移在下方绝对给出
    descs = [
        desc(0, MS_DRAM, DT_F16, (M, K)),
        desc(1, MS_DRAM, DT_F16, (M,)),
        desc(2, MS_DRAM, DT_I8, (M, K)),
        desc(10, MS_SRAM, DT_F16, (M, K), base=0),
        desc(11, MS_SRAM, DT_F16, (M,), base=M * K * 2),
        desc(12, MS_SRAM, DT_I8, (M, K), base=M * K * 2 + 64),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0),
        instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_QUANT, 12, 10, 11, 0),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 2, 12, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "quant", descs, code, inputs=(0, 1), output_id=2)
    return x, sa, out


def test_quant_device_vs_numpy(tmp):
    x, sa, out = make_quant_case(tmp, 32, 64)
    ref = ref_quant(x, sa)
    # RNE(nearbyint)== numpy rint(RNE),fp32 逐步一致 → 逐比特
    assert np.array_equal(out.astype(np.int8), ref), \
        f"量化结果不一致:{np.argwhere(out != ref)[:4]}"
    # 饱和度检查:构造 |x| > 127*sa 的越界行(absmax 行恰好在界内,放大一行)
    print("PASS test_quant_device_vs_numpy (bitwise == numpy RNE)")


def test_quant_saturation(tmp):
    M, K = 32, 64
    rng = np.random.default_rng(7)
    x = (rng.standard_normal((M, K))).astype(np.float16)
    sa = (np.abs(x.astype(np.float32)).max(axis=1) / 127.0).astype(np.float16)
    x[3, :] = (sa[3] * 200).astype(np.float16)  # 远超 127*sa → 全部饱和
    np.save(os.path.join(tmp, "in0.npy"), x)
    np.save(os.path.join(tmp, "in1.npy"), sa)
    descs = [
        desc(0, MS_DRAM, DT_F16, (M, K)),
        desc(1, MS_DRAM, DT_F16, (M,)),
        desc(2, MS_DRAM, DT_I8, (M, K)),
        desc(10, MS_SRAM, DT_F16, (M, K), base=0),
        desc(11, MS_SRAM, DT_F16, (M,), base=M * K * 2),
        desc(12, MS_SRAM, DT_I8, (M, K), base=M * K * 2 + 64),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_QUANT, 12, 10, 11, 0),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 2, 12, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "quant_sat", descs, code, inputs=(0, 1), output_id=2)
    assert np.all(out[3] == 127), f"饱和失败:{out[3][:8]}"
    assert np.abs(out).max() <= 127 and out.dtype == np.int8
    print("PASS test_quant_saturation (行 3 全部 +127)")


def _load_gemm_i8_dram(tmp, M, K, N, A, B):
    np.save(os.path.join(tmp, "in0.npy"), A)
    np.save(os.path.join(tmp, "in1.npy"), B)


def test_gemm_i8_exact(tmp):
    M, K, N = 32, 64, 32
    rng = np.random.default_rng(1)
    A = rng.integers(-127, 128, (M, K), dtype=np.int8)
    B = rng.integers(-127, 128, (K, N), dtype=np.int8)
    _load_gemm_i8_dram(tmp, M, K, N, A, B)
    descs = [
        desc(0, MS_DRAM, DT_I8, (M, K)),
        desc(1, MS_DRAM, DT_I8, (K, N)),
        desc(2, MS_DRAM, DT_I32, (M, N)),
        desc(10, MS_SRAM, DT_I8, (M, K), base=0),
        desc(11, MS_SRAM, DT_I8, (K, N), base=M * K),
        desc(12, MS_SRAM, DT_I32, (M, N), base=M * K + K * N),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0),
        instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_GEMM_I8, 12, 10, 11, (N << 16) | M),
        gemm_w1(K, acc_mode=0),
        instr(OP_BAR, 0, 0, 0, CH_GEMM),
        instr(OP_DMA_ST, 2, 12, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "gemm_i8", descs, code, inputs=(0, 1), output_id=2)
    ref = A.astype(np.int32) @ B.astype(np.int32)
    assert out.dtype == np.int32
    assert np.array_equal(out, ref), "GEMM_I8 必须 bit-exact 等于 numpy int32"
    print("PASS test_gemm_i8_exact (bit-exact vs numpy int32)")


def test_gemm_i8_acc_tiles(tmp):
    M, K, N, Tk = 32, 64, 32, 32
    rng = np.random.default_rng(2)
    A = rng.integers(-127, 128, (M, K), dtype=np.int8)
    B = rng.integers(-127, 128, (K, N), dtype=np.int8)
    _load_gemm_i8_dram(tmp, M, K, N, A, B)
    descs = [
        desc(0, MS_DRAM, DT_I8, (M, K)),
        desc(1, MS_DRAM, DT_I8, (K, N)),
        desc(2, MS_DRAM, DT_I32, (M, N)),
        desc(10, MS_SRAM, DT_I8, (M, Tk), base=0),          # A tile 槽
        desc(11, MS_SRAM, DT_I8, (Tk, N), base=M * Tk + 64),  # B tile 槽
        desc(12, MS_SRAM, DT_I32, (M, N), base=M * Tk + Tk * N + 128),
    ]
    code = []
    for kt in range(K // Tk):
        # A 片 imm = kt*Tk(i8:1B/elem,对齐 32 ✓);B 片 imm = kt*Tk*N
        code += [
            instr(OP_DMA_LD, 10, 0, 0, kt * Tk),
            instr(OP_DMA_LD, 11, 1, 0, kt * Tk * N),
            instr(OP_BAR, 0, 0, 0, CH_LD),
            instr(OP_GEMM_I8, 12, 10, 11, (N << 16) | M),
            gemm_w1(Tk, acc_mode=1 if kt > 0 else 0),
            instr(OP_BAR, 0, 0, 0, CH_GEMM),
        ]
    code += [
        instr(OP_DMA_ST, 2, 12, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "gemm_i8_acc", descs, code, inputs=(0, 1), output_id=2)
    ref = A.astype(np.int32) @ B.astype(np.int32)
    assert np.array_equal(out, ref), "跨 Tk tile 的 i32 累加必须 bit-exact"
    print("PASS test_gemm_i8_acc_tiles (2 tiles, bit-exact)")


def test_worst_case_bound(tmp):
    # |acc| 上界 = K*127² = 512*16129 = 8,258,048 ≪ 2³¹(§6.5 论证的算例化)
    M, K, N = 16, 512, 32
    A = np.full((M, K), 127, dtype=np.int8)
    B = np.full((K, N), 127, dtype=np.int8)
    _load_gemm_i8_dram(tmp, M, K, N, A, B)
    descs = [
        desc(0, MS_DRAM, DT_I8, (M, K)),
        desc(1, MS_DRAM, DT_I8, (K, N)),
        desc(2, MS_DRAM, DT_I32, (M, N)),
        desc(10, MS_SRAM, DT_I8, (M, K), base=0),
        desc(11, MS_SRAM, DT_I8, (K, N), base=M * K),
        desc(12, MS_SRAM, DT_I32, (M, N), base=M * K + K * N),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_GEMM_I8, 12, 10, 11, (N << 16) | M),
        gemm_w1(K, acc_mode=0),
        instr(OP_BAR, 0, 0, 0, CH_GEMM),
        instr(OP_DMA_ST, 2, 12, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "gemm_i8_max", descs, code, inputs=(0, 1), output_id=2)
    assert np.all(out == 512 * 127 * 127), f"上界算例错:{out.flat[0]}"
    print(f"PASS test_worst_case_bound (acc={out.flat[0]} == 512·127²)")


def test_dequant_device_vs_numpy(tmp):
    M, N = 32, 32
    rng = np.random.default_rng(3)
    C = rng.integers(-(1 << 20), 1 << 20, (M, N), dtype=np.int32)
    sb = (rng.random(N).astype(np.float32) * 1e-4).astype(np.float16)
    sa = (rng.random(M).astype(np.float32) * 1e-4).astype(np.float16)
    np.save(os.path.join(tmp, "in0.npy"), C)
    np.save(os.path.join(tmp, "in1.npy"), sb)
    np.save(os.path.join(tmp, "in2.npy"), sa)
    cB = M * N * 4
    descs = [
        desc(0, MS_DRAM, DT_I32, (M, N)),
        desc(1, MS_DRAM, DT_F16, (N,)),
        desc(2, MS_DRAM, DT_F16, (M,)),
        desc(3, MS_DRAM, DT_F16, (M, N)),
        desc(10, MS_SRAM, DT_I32, (M, N), base=0),
        desc(11, MS_SRAM, DT_F16, (N,), base=cB),
        desc(12, MS_SRAM, DT_F16, (M,), base=cB + 64),
        desc(13, MS_SRAM, DT_F16, (M, N), base=cB + 128),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0), instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_DMA_LD, 12, 2, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        # 第 4 操作数 sa(id=12)经 imm_hi 低 8 bit(§6.5)
        instr(OP_V_DEQUANT, 13, 10, 11, 12 << 16),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 3, 13, 0, 0),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out = run_case(tmp, "dequant", descs, code, inputs=(0, 1, 2), output_id=3)
    ref = (C.astype(np.float32) * sb.astype(np.float32)[None, :]
           * sa.astype(np.float32)[:, None]).astype(np.float16)
    np.testing.assert_allclose(out.astype(np.float32), ref.astype(np.float32),
                               atol=1e-9, rtol=1e-6)
    print("PASS test_dequant_device_vs_numpy (fp32 参考,≤1ulp 口径)")


def test_quant_gemm_dequant_chain(tmp):
    # 三指令闭环:quant(f16→i8) → GEMM_I8(i32) → dequant(→f16)
    M, K, N = 32, 64, 32
    rng = np.random.default_rng(4)
    x = (rng.standard_normal((M, K))).astype(np.float16)
    sa = (np.abs(x.astype(np.float32)).max(axis=1) / 127.0).astype(np.float16)
    W = (rng.standard_normal((K, N))).astype(np.float16)
    sb = (np.abs(W.astype(np.float32)).max(axis=0) / 127.0).astype(np.float16)
    B = np.clip(np.rint(W.astype(np.float32)
                        / sb.astype(np.float32)[None, :]), -127, 127).astype(np.int8)
    np.save(os.path.join(tmp, "in0.npy"), x)
    np.save(os.path.join(tmp, "in1.npy"), sa)
    np.save(os.path.join(tmp, "in2.npy"), B)
    np.save(os.path.join(tmp, "in3.npy"), sb)
    # sram 槽位(偏移 32B 对齐)
    o_x, o_sa, o_a, o_b, o_sb, o_c, o_y = 0, 4096, 4224, 8320, 10368, 10432, 14528
    descs = [
        desc(0, MS_DRAM, DT_F16, (M, K)),
        desc(1, MS_DRAM, DT_F16, (M,)),
        desc(2, MS_DRAM, DT_I8, (K, N)),
        desc(3, MS_DRAM, DT_F16, (N,)),
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
    out = run_case(tmp, "chain", descs, code, inputs=(0, 1, 2, 3), output_id=4)
    A = ref_quant(x, sa)
    ref = (A.astype(np.int32) @ B.astype(np.int32)).astype(np.float32) \
        * sb.astype(np.float32)[None, :] * sa.astype(np.float32)[:, None]
    # 误差只有最后一次 f16 量化(≤1 ulp ≈ 输出量级×2^-11);对照 f16 C 块路径
    # 每个 Tk 边界都被量化(§2.3)
    scale = np.abs(ref).max()
    np.testing.assert_allclose(out.astype(np.float32), ref,
                               atol=2 * scale * 2**-11, rtol=1e-2)
    err = np.abs(out.astype(np.float32) - ref).max()
    print(f"PASS test_quant_gemm_dequant_chain (max_abs_err={err:.3e})")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_quant_device_vs_numpy(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_quant_saturation(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_gemm_i8_exact(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_gemm_i8_acc_tiles(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_worst_case_bound(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_dequant_device_vs_numpy(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_quant_gemm_dequant_chain(tmp)
    print("ALL test_numerics_i8 PASS")
    sys.exit(0)
