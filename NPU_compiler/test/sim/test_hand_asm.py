#!/usr/bin/env python3
"""步骤 0/1 验收(README §13):手工汇编 minisa,不经编译器,跑通模拟器。

  test_vec_add : 两条 DMA_LD + V_ADD + DMA_ST,对上 numpy
  test_gemm    : M=N=K=64, Tm=Tn=Tk=32, 双缓冲模板,验证 acc_mode 与重叠
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
OP_DMA_LD, OP_DMA_ST, OP_GEMM = 0x01, 0x02, 0x10
OP_V_ADD = 0x23
OP_BAR, OP_END = 0xF0, 0xF1
MS_DRAM, MS_SRAM = 0, 1
DT_F16 = 1
CH_LD, CH_ST, CH_GEMM, CH_VEC = 1, 2, 4, 8


def instr(op, dst, s0, s1, imm):
    return (op << 56) | (dst << 48) | (s0 << 40) | (s1 << 32) | imm


def gemm_w1(tile_k, acc_mode, a_layout=0, b_layout=0):
    return (tile_k << 48) | (acc_mode << 40) | (a_layout << 32) | (b_layout << 24)


def desc(id, memspace, dtype, shape, base=0):
    rank = len(shape)
    stride = [1] * 4
    for i in range(rank - 2, -1, -1):  # 行主序
        stride[i] = stride[i + 1] * shape[i + 1]
    shp = list(shape) + [1] * (4 - rank)
    es = 2 if dtype == DT_F16 else 4
    nbytes = es
    for s in shape:
        nbytes *= s
    return struct.pack("<IBBBB4I4i2Q", id, memspace, dtype, rank, 32,
                       *shp, *stride, base, nbytes)


def write_minisa(path, descs, code_words, constants=b""):
    code = b"".join(struct.pack("<Q", w) for w in code_words)
    hdr = struct.pack("<4sHHIII", b"MNPU", 1, 36, len(descs), len(code),
                      len(constants)) + b"\0" * 16
    with open(path, "wb") as f:
        f.write(hdr)
        for d in descs:
            f.write(d)
        f.write(code)
        f.write(constants)


def run_case(tmp, name, descs, code, inputs, outputs, consts=None):
    write_minisa(os.path.join(tmp, f"{name}.minisa"), descs, code)
    meta = {"entry": name,
            "inputs": [{"id": i, "npy": f"in{i}.npy"} for i in inputs],
            "outputs": [{"id": o} for o in outputs],
            "consts": consts or []}
    with open(os.path.join(tmp, f"{name}.meta.json"), "w") as f:
        json.dump(meta, f)
    out_path = os.path.join(tmp, f"{name}_out.npy")
    r = subprocess.run(
        [MINIRT, "--bin", os.path.join(tmp, f"{name}.minisa"),
         "--spec", SPEC, "--dump-out", out_path,
         "--dump-timeline", os.path.join(tmp, f"{name}_tl.json")],
        capture_output=True, text=True)
    assert r.returncode == 0, f"minirt failed:\n{r.stderr}"
    return np.load(out_path), json.load(open(os.path.join(tmp, f"{name}_tl.json")))


def test_vec_add(tmp):
    S, H = 32, 256
    rng = np.random.default_rng(0)
    x = rng.standard_normal((S, H)).astype(np.float16)
    w = rng.standard_normal((S, H)).astype(np.float16)
    np.save(os.path.join(tmp, "in0.npy"), x)
    np.save(os.path.join(tmp, "in1.npy"), w)

    row_bytes = S * H * 2
    descs = [
        desc(0, MS_DRAM, DT_F16, (S, H)),          # x
        desc(1, MS_DRAM, DT_F16, (S, H)),          # w
        desc(2, MS_DRAM, DT_F16, (S, H)),          # out
        desc(10, MS_SRAM, DT_F16, (S, H), base=0),
        desc(11, MS_SRAM, DT_F16, (S, H), base=row_bytes),
        desc(12, MS_SRAM, DT_F16, (S, H), base=2 * row_bytes),
    ]
    code = [
        instr(OP_DMA_LD, 10, 0, 0, 0),
        instr(OP_DMA_LD, 11, 1, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_LD),
        instr(OP_V_ADD, 12, 10, 11, 0),
        instr(OP_BAR, 0, 0, 0, CH_VEC),
        instr(OP_DMA_ST, 2, 12, 0, 0),
        instr(OP_BAR, 0, 0, 0, CH_ST),
        instr(OP_END, 0, 0, 0, 0),
    ]
    out, _ = run_case(tmp, "vecadd", descs, code, inputs=(0, 1), outputs=(2,))
    ref = x.astype(np.float32) + w.astype(np.float32)
    np.testing.assert_allclose(out.astype(np.float32), ref, atol=1e-3, rtol=1e-2)
    print("PASS test_vec_add")


def test_gemm(tmp):
    M = N = K = 64
    Tm = Tn = Tk = 32
    rng = np.random.default_rng(1)
    A = rng.standard_normal((M, K)).astype(np.float16)
    B = rng.standard_normal((K, N)).astype(np.float16)
    np.save(os.path.join(tmp, "in0.npy"), A)
    np.save(os.path.join(tmp, "in1.npy"), B)

    # SRAM 槽位:A0/A1/B0/B1 各 2KiB,C 2KiB
    a0, a1, b0, b1, c = 0, 2048, 4096, 6144, 8192
    descs = [
        desc(0, MS_DRAM, DT_F16, (M, K)),   # A
        desc(1, MS_DRAM, DT_F16, (K, N)),   # B
        desc(2, MS_DRAM, DT_F16, (M, N)),   # C
        desc(10, MS_SRAM, DT_F16, (Tm, Tk), base=a0),
        desc(11, MS_SRAM, DT_F16, (Tm, Tk), base=a1),
        desc(12, MS_SRAM, DT_F16, (Tk, Tn), base=b0),
        desc(13, MS_SRAM, DT_F16, (Tk, Tn), base=b1),
        desc(14, MS_SRAM, DT_F16, (Tm, Tn), base=c),
    ]
    # DRAM 字节偏移:A(mt,k)=mt*Tm*K*2+k*Tk*2;B(k,nt)=k*Tk*N*2+nt*Tn*2;
    #              C(mt,nt)=mt*Tm*N*2+nt*Tn*2(fp16 行主序)
    aoff = lambda mt, k: mt * Tm * K * 2 + k * Tk * 2
    boff = lambda k, nt: k * Tk * N * 2 + nt * Tn * 2
    coff = lambda mt, nt: mt * Tm * N * 2 + nt * Tn * 2
    aslot = [10, 11]
    bslot = [12, 13]
    code = []
    for mt in range(M // Tm):
        for nt in range(N // Tn):
            code += [
                instr(OP_DMA_LD, aslot[0], 0, 0, aoff(mt, 0)),
                instr(OP_DMA_LD, bslot[0], 1, 0, boff(0, nt)),
                instr(OP_BAR, 0, 0, 0, CH_LD),
            ]
            for k in range(K // Tk):
                if k + 1 < K // Tk:  # 预取下一 k 到另一个槽位(双缓冲)
                    code += [
                        instr(OP_DMA_LD, aslot[(k + 1) % 2], 0, 0, aoff(mt, k + 1)),
                        instr(OP_DMA_LD, bslot[(k + 1) % 2], 1, 0, boff(k + 1, nt)),
                    ]
                code += [
                    instr(OP_GEMM, 14, aslot[k % 2], bslot[k % 2],
                          (Tn << 16) | Tm),
                    gemm_w1(Tk, acc_mode=1 if k > 0 else 0),
                    instr(OP_BAR, 0, 0, 0, CH_GEMM | CH_LD),
                ]
            code += [
                instr(OP_DMA_ST, 2, 14, 0, coff(mt, nt)),
                # 等 ST 完成再让下一输出块的 GEMM 覆写 C 槽
                instr(OP_BAR, 0, 0, 0, CH_ST),
            ]
    code.append(instr(OP_END, 0, 0, 0, 0))
    out, tl = run_case(tmp, "gemm", descs, code, inputs=(0, 1), outputs=(2,))
    ref = A.astype(np.float32) @ B.astype(np.float32)
    # 注意:K 分成 2 个 Tk 块,中间部分和以 fp16 形式驻留 SRAM(C desc 为 f16),
    # 量化误差 ~ulp(部分和量级)≈4e-3,对近零输出远超 atol=1e-3。
    # 这是硬件设计(fp16 C 块)的固有性质,故 GEMM 用 atol=1e-2。
    np.testing.assert_allclose(out.astype(np.float32), ref, atol=1e-2, rtol=1e-2)

    # 双缓冲重叠:k=1 的 DMA_LD 必须与 k=0 的 GEMM 时间重叠(README §2.4)
    evs = [(e["name"], e["ts"], e["ts"] + e["dur"]) for e in tl["traceEvents"]]
    gemm0 = next(e for e in evs if e[0] == "GEMM")
    lds_k1 = [e for e in evs if e[0] == "DMA_LD" and e[1] >= gemm0[1] - 1]
    assert lds_k1, "时间线上看不到 DMA/GEMM 重叠"
    overlap = min(gemm0[2], max(e[2] for e in lds_k1)) - max(gemm0[1], min(e[1] for e in lds_k1))
    assert overlap > 0, "DMA_LD(k=1) 与 GEMM(k=0) 无重叠"
    print(f"PASS test_gemm (overlap={overlap} cycles)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_vec_add(tmp)
        test_gemm(tmp)
    print("ALL HAND-ASM TESTS PASS")
