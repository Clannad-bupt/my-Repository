#!/usr/bin/env python3
# 对 K=11008/gs=128 dry-run case 的 golden 做 numpy 语义复算校验。
# 口径（与 compute_bank2_golden_kchunk 一致）：
#   每 k-block(32)：partial[r,c] = Σ_k fp16rt(a[r,k]) * fp16rt(bq[k,c]-zp[g,c])  (fp32)
#   bank7[r,c] += partial[r,c] * fp16rt(scale[g,c])   (fp32 跨块累加)
# golden hex：每行 16B 字节整体倒序；倒回后 word w = 行 w，fp32 LE lane n = 列 4s+n。
import os
import struct
import sys

import numpy as np

# 用例目录（argv[1]）之外的另一个参数：外部张量目录（argv[2]）。
# 默认从本脚本位置推导，避免写死某个人的家目录：
#   <wksp>/3_tool/tvgen/load_store_v3/MatMul_NT_w4f16/check_golden_k11008.py
#   parents[4] -> <wksp>
#   <wksp>/2_verification/aiacc/testbench/sim/MatMul_NT_w4f16/tensors_k11008_gs128
_WKSP = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
DEFAULT_TDIR = os.path.join(_WKSP, "2_verification", "aiacc", "testbench", "sim",
                            "MatMul_NT_w4f16", "tensors_k11008_gs128")

CASE = sys.argv[1] if len(sys.argv) > 1 else "/tmp/w4f16_k11008_dryrun"
TDIR = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("MM_TENSOR_DIR", DEFAULT_TDIR)
K, GS, N, M = 11008, 128, 64, 8
G = K // GS
CHUNK = 32
NKB = K // CHUNK


def fp16rt(x):
    return np.asarray(x, dtype=np.float16).astype(np.float32)


a = np.fromfile(TDIR + "/a_8x%d_f32.bin" % K, dtype="<f4").reshape(M, K)
bq = np.fromfile(TDIR + "/bq_%dx64_u4.bin" % K, dtype=np.uint8).reshape(K, N)
zp = np.fromfile(TDIR + "/zp_%dx64_u4.bin" % (G), dtype=np.uint8).reshape(G, N)
sc = np.fromfile(TDIR + "/scale_%dx64_f16.bin" % G, dtype="<u2").reshape(G, N).view(np.float16)

a16 = fp16rt(a)  # (8, K)

# 复算 32 列（8 SMC × 4 列）
out = np.zeros((M, 32), dtype=np.float32)
for kb in range(NKB):
    g = (kb * CHUNK) // GS
    k0, k1 = kb * CHUNK, kb * CHUNK + CHUNK
    d = fp16rt(bq[k0:k1, :32].astype(np.int32) - zp[g, :32].astype(np.int32))  # (32, 32)
    partial = np.matmul(a16[:, k0:k1], d.astype(np.float32)).astype(np.float32)  # (8, 32)
    out = (out + partial * fp16rt(sc[g, :32].astype(np.float32))).astype(np.float32)

# 比对每个 SMC 的 golden
nfail = 0
for s in range(8):
    path = "%s/smc_%d/output_bank/bank7_final.hex" % (CASE, s)
    with open(path) as f:
        lines = [ln.strip() for ln in f if ln.strip()]
    gold = np.zeros((M, 4), dtype=np.float32)
    for w in range(8):
        b = bytes.fromhex(lines[w])[::-1]  # 16B 整体倒序
        for n in range(4):
            # 实测：倒回后 offset n*4 上的值 = 列 4s+(3-n)（lane 反序）
            gold[w, 3 - n] = struct.unpack("<f", b[n * 4:n * 4 + 4])[0]
    ref = out[:, 4 * s:4 * s + 4]
    absdiff = np.abs(gold - ref)
    reldiff = absdiff / np.maximum(np.abs(ref), 1e-6)
    ok = np.all(reldiff < 1e-3)
    print("smc_%d: %s  max_rel=%.3g  max_abs=%.3g  ref_absmax=%.3g"
          % (s, "PASS" if ok else "FAIL", reldiff.max(), absdiff.max(), np.abs(ref).max()))
    if not ok:
        nfail += 1
        idx = np.unravel_index(np.argmax(reldiff), reldiff.shape)
        print("   worst @row%d col%d: gold=%s ref=%s"
              % (idx[0], 4 * s + idx[1], gold[idx], ref[idx]))

print("OVERALL:", "PASS" if nfail == 0 else "FAIL (%d smc)" % nfail)
sys.exit(1 if nfail else 0)
