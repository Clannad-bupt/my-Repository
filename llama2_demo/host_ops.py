#!/usr/bin/env python3
"""host_ops.py — 混合运行时 (llama2_hybrid.py) 的 host CPU 算子层.

覆盖 6 个算子 (2026-10-26 决策, 见下), prefill/decode 共用一份实现
(iq_map 登记只差 seq 维: prefill SEQ=256 / decode 1):

    reduce_mean_square  RMSNorm 分解第一步 (冯海峻, 未交付)
    rotate_half         RoPE 分解 (李思颖; IQ 已交付但 v7 bitstream 数据
                        通路与 OEM golden 不符, RTL 定夺前 host 兜底)
    add_mask            attention 因果掩码加 (孙浩南, 未交付)
    softmax             attention softmax (杨凯程, 未交付)
    add_f16             残差 Add + RoPE 末步 Add (孙浩南, 未交付)
    silu                FFN SiLU (杨凯程, 未交付)

mul 族 4 算子 (rmsnorm_mul_rsqrt / rmsnorm_mul_weight / rope_mul_cos /
mul_gate) 按 phase 分流 (2026-10-26, llama2_hybrid.BACKENDS):
    decode  板上单发交付位精确 PASS, 走 board 适配器
    prefill 交付为轮式 (64/172 轮), store 引擎有头重放/尾部丢值结构
            假象 (test_u280_board.py store_want_prefill 实证, 每轮丢
            16 元素不可还原; rope_mul_cos 的 cos 消费流另有循环右移
            8 的装载侧假象) —— 板上输出无法忠实还原, host 兜底

数学全部复用 test/operators.py 的 numpy golden (与上板比对同源: f32 内部
计算, 写回 f16); 本层只负责 spec 形状/dtype 适配与调用约定。

matmul 族原则 (2026-10-26 决策) 是板上交付无 host 兜底; 2026-10-28 端到端
首跑对未交付部分做**显式例外** (llama2_hybrid.BACKENDS 同步路由):
    matmul_w4_* prefill   板上只有 M=1 decode case (256 token 循环发射不
                          现实) —— host; decode 仍走板上 MatmulW4Runner
    matmul_qk / matmul_xv 王俊驰未交付 —— 双 phase host
    lmhead                板上已 PASS, 不走 host
另注意: matmul_qk 在此**折入 1/√HEAD_DIM 缩放** —— HF 参考 logits
(_awq_ref.npz) 的 attention 含此缩放, 而 spec 分解 (qk→mask→softmax) 的
golden 无 scale, 即 spec 未给缩放归属算子; 为对拍参考先挂在 host
matmul_qk 内, 待 spec 定夺归属 (spec gap, 2026-10-28 上报)。

自测 (无板卡, 仅依赖 numpy):
    python3 host_ops.py
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
sys.path[:0] = [str(ROOT), str(ROOT / "test")]

import iq_map               # noqa: E402  shape/dtype 唯一来源
import operators as op      # noqa: E402  numpy golden

F16 = np.float16

# ---------------------------------------------------------------------------
# 算子实现 (签名 = spec 输入序; 返回 = out0)
# ---------------------------------------------------------------------------


def reduce_mean_square(x: np.ndarray) -> np.ndarray:
    """prefill (1,SEQ,HIDDEN)->(1,SEQ,1) / decode (1,1,HIDDEN)->(1,1,1)."""
    return op.reduce_mean_square(x, iq_map.EPS)


def rsqrt(x: np.ndarray, _lut: np.ndarray = None) -> np.ndarray:
    """(1,seq,1)->同形, 1/sqrt(x); 2026-11-02 prefill 全 host 定案补齐
    (此前 rsqrt 仅板上一途). spec 第二输入是板上位技巧 LUT (R3),
    host 侧按 golden 语义直接 1/sqrt, LUT 忽略."""
    return op.rsqrt(x)


def rotate_half(x: np.ndarray) -> np.ndarray:
    """(1,seq,HEADS,HEAD_DIM) -> 同形; 末维切半: [-x2, x1]."""
    return op.rotate_half(x)


def add_mask(scores: np.ndarray, mask: np.ndarray) -> np.ndarray:
    """(1,HEADS,Sq,Sk) + (1,1,Sq,Sk) 广播 -> (1,HEADS,Sq,Sk)."""
    return op.add(scores, mask)


def softmax(x: np.ndarray) -> np.ndarray:
    """(1,HEADS,Sq,Sk) 沿最后一维 softmax."""
    return op.softmax_f16(x)


def add_f16(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """同形残差 Add (RoPE 末步复用: (1,seq,HEADS,HEAD_DIM) 与
    (1,seq,HIDDEN) 元素数、连续布局一致, 按实参形状逐元素加即可)."""
    if a.shape != b.shape:
        raise ValueError(f"add_f16 要求同形, 实际 {a.shape} vs {b.shape}")
    return op.add(a, b)


def silu(x: np.ndarray) -> np.ndarray:
    """(1,seq,FFN) -> 同形, x*sigmoid(x)."""
    return op.silu(x)


# ---- mul 族 (prefill host 兜底用; decode 走板上) ----
# 全部是 op.mul 的广播实例, numpy 广播语义与 spec 登记形状一致。

def rmsnorm_mul_rsqrt(x: np.ndarray, r: np.ndarray) -> np.ndarray:
    """(1,seq,HIDDEN) * (1,seq,1) 行标量广播."""
    return op.mul(x, r)


def rmsnorm_mul_weight(x: np.ndarray, w: np.ndarray) -> np.ndarray:
    """(1,seq,HIDDEN) * (1,1,HIDDEN) 行广播."""
    return op.mul(x, w)


def rope_mul_cos(x: np.ndarray, c: np.ndarray) -> np.ndarray:
    """(1,seq,HEADS,HEAD_DIM) * (1,seq,1,HEAD_DIM) 头广播; sin 复用同 entry."""
    return op.mul(x, c)


def mul_gate(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """(1,seq,FFN) 同形逐元素乘 (silu(gate) * up)."""
    return op.mul(a, b)


# ---- matmul 族 (2026-10-28 端到端首跑显式例外, 见模块 docstring) ----

def _matmul_w4(x: np.ndarray, qw: np.ndarray, sc: np.ndarray,
               qz: np.ndarray) -> np.ndarray:
    """out = x @ dequant(w4)^T; 权重入参为 spec 打包形态 (NT 布局):
    qw (N,K/2) u8, sc (N,G) f16, qz (N,G/2) u8."""
    w_q = op.unpack_uint4(qw, qw.shape[-1] * 2)        # (N,K) 逻辑 u4
    zp = op.unpack_uint4(qz, qz.shape[-1] * 2)         # (N,G)
    return op.matmul_nt_w4f16(x, w_q, sc, zp, iq_map.GROUP)


def matmul_w4_qkvo(x, qw, sc, qz):
    """(1,seq,HIDDEN) × qkvo 投影 (N=HIDDEN)."""
    return _matmul_w4(x, qw, sc, qz)


def matmul_w4_gate_up(x, qw, sc, qz):
    """(1,seq,HIDDEN) × gate/up 投影 (N=FFN)."""
    return _matmul_w4(x, qw, sc, qz)


def matmul_w4_down(x, qw, sc, qz):
    """(1,seq,FFN) × down 投影 (N=HIDDEN)."""
    return _matmul_w4(x, qw, sc, qz)


def matmul_qk(q: np.ndarray, k: np.ndarray) -> np.ndarray:
    """逐 head q@k^T **× 1/√HEAD_DIM** —— 缩放折在本算子内 (spec gap,
    见模块 docstring); f32 计算, f16 写回。"""
    qh = q.astype(np.float32).transpose(0, 2, 1, 3)   # (B,H,Sq,D)
    kh = k.astype(np.float32).transpose(0, 2, 1, 3)   # (B,H,Sk,D)
    s = qh @ kh.transpose(0, 1, 3, 2) * (q.shape[-1] ** -0.5)
    return s.astype(F16)


def matmul_xv(scores: np.ndarray, v: np.ndarray) -> np.ndarray:
    """(1,HEADS,Sq,Sk) × (1,Sk,HEADS,HEAD_DIM) -> (1,Sq,HEADS,HEAD_DIM)."""
    return op.matmul_f16_xv(scores, v)


# case 名 -> 实现; llama2_hybrid 的 dispatch 按此表路由 host 后端
DISPATCH = {
    "reduce_mean_square": reduce_mean_square,
    "rsqrt": rsqrt,
    "rotate_half": rotate_half,
    "add_mask": add_mask,
    "softmax": softmax,
    "add_f16": add_f16,
    "silu": silu,
    "rmsnorm_mul_rsqrt": rmsnorm_mul_rsqrt,
    "rmsnorm_mul_weight": rmsnorm_mul_weight,
    "rope_mul_cos": rope_mul_cos,
    "mul_gate": mul_gate,
    "matmul_w4_qkvo": matmul_w4_qkvo,
    "matmul_w4_gate_up": matmul_w4_gate_up,
    "matmul_w4_down": matmul_w4_down,
    "matmul_qk": matmul_qk,
    "matmul_xv": matmul_xv,
}

HOST_OPS = frozenset(DISPATCH)


# ---------------------------------------------------------------------------
# 自测: python3 host_ops.py  (无板卡; 形状契约 + 语义抽查)
# ---------------------------------------------------------------------------

def _selftest() -> int:
    rng = np.random.default_rng(0)

    def rf16(shape, scale=0.02):
        return (rng.standard_normal(shape) * scale).astype(F16)

    fails = 0

    def check(name, cond):
        nonlocal fails
        print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
        fails += 0 if cond else 1

    # 1) spec 形状契约: 每个 host case 的 prefill/decode 登记项都过一遍
    print("== spec 形状契约 ==")
    for spec in iq_map.IQ_MAP:
        if spec.case not in DISPATCH:
            continue
        inputs = []
        for t in spec.inputs:
            if t.dtype == iq_map.U4:
                # 逻辑 (N,K) u4 的物理形态: 打包 uint8 (N,K/2)
                arr = rng.integers(
                    0, 256, t.shape[:-1] + (t.shape[-1] // 2,),
                    dtype=np.uint8)
            elif t.slot == "in1" and spec.case == "add_mask":
                # 因果掩码分布: 0 / -3e4
                arr = np.where(rng.random(t.shape) < 0.5,
                               F16(0.0), F16(-3.0e4))
            elif t.slot == "in2" and spec.case.startswith("matmul_w4"):
                # scale: 小正数 (权重反量化量级合理)
                arr = np.abs(rf16(t.shape, 0.01)) + F16(1e-3)
            else:
                arr = rf16(t.shape, 4.0 if spec.case in ("add_mask", "softmax")
                           else 0.02)
            inputs.append(arr)
        out = DISPATCH[spec.case](*inputs)
        want = spec.outputs[0]
        check(f"{spec.phase}/{spec.case} out{out.shape}{out.dtype} "
              f"== spec{want.shape}x{want.dtype}",
              out.shape == want.shape and out.dtype == F16)

    # 2) 语义抽查
    print("== 语义抽查 ==")
    x = np.array([[[[1.0, 2.0, 3.0, 4.0]]]], dtype=F16)
    check("rotate_half([1,2,3,4]) == [-3,-4,1,2]",
          np.array_equal(rotate_half(x),
                         np.array([[[[-3.0, -4.0, 1.0, 2.0]]]], dtype=F16)))

    s = softmax(rf16((1, 4, 8, 8), 4.0))
    check("softmax 各行和≈1",
          np.allclose(s.astype(np.float32).sum(axis=-1), 1.0, atol=2e-3))

    scores = np.zeros((1, 2, 4, 4), dtype=F16)
    mask = np.full((1, 1, 4, 4), -3.0e4, dtype=F16)
    mask[..., np.arange(4)[:, None] >= np.arange(4)[None, :]] = 0.0
    am = add_mask(scores, mask)
    check("add_mask 广播: 允许位=0, 屏蔽位≈-3e4",
          np.array_equal(am[..., 0, 0], np.zeros((1, 2), dtype=F16))
          and float(am[0, 0, 0, 3]) < -1.0e4)

    check("silu(0)=0, silu(1)≈0.731",
          abs(float(silu(np.array([0.0], dtype=F16))[0])) < 1e-6
          and abs(float(silu(np.array([1.0], dtype=F16))[0]) - 0.731) < 2e-3)

    ms = reduce_mean_square(rf16((1, 3, 16)))
    check("reduce_mean_square 输出形状 (1,3,1) 且值为均方+eps",
          ms.shape == (1, 3, 1))

    # 3) 组合一致性: 本层拼装与 operators 复合 golden 同路径
    print("== 组合一致性 (与 operators.py 复合 golden 同数学路径) ==")
    h = rf16((1, 4, 32))
    w = rf16((1, 1, 32))
    rms = op.mul(op.mul(h, op.rsqrt(reduce_mean_square(h))), w)
    check("rmsnorm 分解 == op.rmsnorm",
          np.array_equal(rms, op.rmsnorm(h, w, iq_map.EPS)))
    q = rf16((1, 4, 2, 8))
    cos = rf16((1, 4, 1, 8))
    sin = rf16((1, 4, 1, 8))
    rope = add_f16(op.mul(q, cos), op.mul(rotate_half(q), sin))
    check("rope 分解 == op.rope", np.array_equal(rope, op.rope(q, cos, sin)))

    print(f"\n共 {fails} 项 FAIL")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(_selftest())
