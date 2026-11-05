#!/usr/bin/env python3
# 生成 MatMul_NT_w4f16 外部张量（MM_TENSOR_DIR 路径），用于真实 K / gs 切片验证。
#
# 用法:
#   python3 gen_ext_tensors.py <out_dir> [K] [GS] [M] [seed] [N]
# 默认: K=11008 GS=128 M=8 seed=42 N=64
#
# 文件格式（与 matmul_nt_w4f16_lsu_op.cpp ensure_external_loaded 的约定一致）:
#   a_{M}x{K}_f32.bin      : fp32 LE，行主序 M×K
#   bq_{K}x{N}_u4.bin      : 每元素 1 字节（值 0..15，未 pack 成 nibble），行主序 K×N
#   zp_{G}x{N}_u4.bin      : 每元素 1 字节（0..15），行主序 G×N，G=K/GS
#   scale_{G}x{N}_f16.bin  : fp16 bit 的 uint16 LE，行主序 G×N
import os
import sys

import numpy as np


def main():
    out_dir = sys.argv[1]
    K = int(sys.argv[2]) if len(sys.argv) > 2 else 11008
    GS = int(sys.argv[3]) if len(sys.argv) > 3 else 128
    M = int(sys.argv[4]) if len(sys.argv) > 4 else 8
    seed = int(sys.argv[5]) if len(sys.argv) > 5 else 42
    N = int(sys.argv[6]) if len(sys.argv) > 6 else 64
    G = K // GS
    assert K % GS == 0, "K 必须整除 GS"
    assert N % 32 == 0, "N 必须 32 的倍数（每 tile 32 列）"

    rng = np.random.RandomState(seed)

    # A: fp32 激活，幅度 0.5 避免 K=11008 累加后超出 fp16 输出范围。
    # 要求打包成 fp16 后必须是规格化数（FPGA fp16 单元对非规格化输入 FTZ），
    # 转 fp16 等于 0 或小于最小规格化数的元素整批重采样，直至全部为规格化数。
    f16_tiny = np.finfo(np.float16).tiny  # 最小规格化 fp16 = 2**-14
    a = rng.randn(M, K) * 0.5
    h16 = a.astype(np.float16)
    redraws = 0
    while True:
        bad = (h16 == 0) | (np.abs(h16) < f16_tiny)
        if not bad.any():
            break
        redraws += int(bad.sum())
        h16[bad] = (rng.randn(int(bad.sum())) * 0.5).astype(np.float16)
    a = h16.astype("<f4")  # 回写 f32 文件，值与打包 fp16 逐位一致
    print("A fp16-normalized: redrew %d element(s) (|x|<2^-14 or 0)" % redraws)
    # B: w4 量化权重，每元素一字节 0..15
    bq = rng.randint(0, 16, size=(K, N)).astype(np.uint8)
    # zp: 每元素一字节 0..15
    zp = rng.randint(0, 16, size=(G, N)).astype(np.uint8)
    # scale: fp16 bit 的 uint16 LE（必须 view 而非 astype，否则截断成整数）
    scale = (0.01 + 0.04 * rng.rand(G, N)).astype(np.float16)
    assert np.all(np.abs(scale) >= f16_tiny), "scale 出现非规格化 fp16"

    os.makedirs(out_dir, exist_ok=True)
    a.tofile(os.path.join(out_dir, "a_%dx%d_f32.bin" % (M, K)))
    bq.tofile(os.path.join(out_dir, "bq_%dx%d_u4.bin" % (K, N)))
    zp.tofile(os.path.join(out_dir, "zp_%dx%d_u4.bin" % (G, N)))
    scale.view("<u2").tofile(os.path.join(out_dir, "scale_%dx%d_f16.bin" % (G, N)))

    print("written to %s:" % out_dir)
    for f in sorted(os.listdir(out_dir)):
        print("  %s  %d bytes" % (f, os.path.getsize(os.path.join(out_dir, f))))
    print("M=%d K=%d N=%d GS=%d G=%d seed=%d" % (M, K, N, GS, G, seed))


if __name__ == "__main__":
    main()
