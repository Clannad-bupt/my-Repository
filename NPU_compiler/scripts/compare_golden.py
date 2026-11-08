#!/usr/bin/env python3
"""对比 NPU 输出与 PyTorch 黄金参考（README §11.2）。

minirt 只负责 dump 输出 tensor，数值对比统一走本脚本。
默认按 FP16 容差（§2.3）；含 GEMM 的图用 --atol 1e-2 --rtol 1e-2
（FP16 C 块跨 Tk 块部分和量化，§2.3）；FP32 对照实验用 --atol 1e-4 --rtol 1e-3。
"""
import argparse
import sys

import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", help="minirt --dump-out 产物（.npy）")
    ap.add_argument("--ref", required=True, help="黄金参考（.npy，FP32）")
    ap.add_argument("--atol", type=float, default=1e-3)
    ap.add_argument("--rtol", type=float, default=1e-2)
    # 二期-INT8(§2.3):量化图用相对指标(绝对容差在零中心输出上是空洞)
    ap.add_argument("--cos", type=float, default=None,
                    help="cosine 相似度下界(如 0.999)")
    ap.add_argument("--rel-l2", type=float, default=None,
                    help="相对 L2 误差上界 ||out-ref||/||ref||(如 2e-2)")
    args = ap.parse_args()

    out = np.load(args.out).astype(np.float32)
    ref = np.load(args.ref).astype(np.float32)
    if out.shape != ref.shape:
        sys.exit(f"FAIL: shape mismatch {out.shape} vs {ref.shape}")

    err = np.abs(out - ref)
    flat_o, flat_r = out.ravel(), ref.ravel()
    cos = float(np.dot(flat_o, flat_r) /
                (np.linalg.norm(flat_o) * np.linalg.norm(flat_r) + 1e-30))
    rel = float(np.linalg.norm(flat_o - flat_r) /
                (np.linalg.norm(flat_r) + 1e-30))
    if args.cos is not None or args.rel_l2 is not None:
        ok = True
        msg = []
        if args.cos is not None:
            ok &= cos >= args.cos
            msg.append(f"cos={cos:.6f}(>= {args.cos})")
        if args.rel_l2 is not None:
            ok &= rel <= args.rel_l2
            msg.append(f"rel_l2={rel:.3e}(<= {args.rel_l2})")
        print(("PASS" if ok else "FAIL") + ": " + " ".join(msg) +
              f"  [max_abs_err={err.max():.3e}]")
        sys.exit(0 if ok else 1)

    np.testing.assert_allclose(out, ref, atol=args.atol, rtol=args.rtol)
    print(f"PASS: max_abs_err={err.max():.3e} mean_abs_err={err.mean():.3e} "
          f"(atol={args.atol}, rtol={args.rtol}, cos={cos:.6f}, rel_l2={rel:.3e})")


if __name__ == "__main__":
    main()
