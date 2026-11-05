from __future__ import annotations

import numpy as np
import argparse

import time
from transformers import AutoTokenizer
from enum import Enum

from llm_read import LlmRead
from u280 import U280
from iq_map import *

ADDR_ALIGN = 0x1000  # every weight starts on a 4KB boundary in HBM


class WeightType(Enum):
    EMBED = "embed"
    NORM = "norm"
    QWEIGHT = "qweight"
    QZEROS = "qzeros"
    SCALES = "scales"
    LM_HEAD = "lm_head"
    HOST = "host"


class KeySpec:
    def __init__(self, key: str, addr: int, nbytes: int, wtype: WeightType):
        self.key = key      # key in safetensors file
        self.addr = addr    # HBM address
        self.nbytes = nbytes
        self.wtype = wtype


def weight_type_of(key: str) -> WeightType:
    if key.endswith(".qweight"):
        return WeightType.QWEIGHT
    if key.endswith(".qzeros"):
        return WeightType.QZEROS
    if key.endswith(".scales"):
        return WeightType.SCALES
    if "embed_tokens" in key:
        return WeightType.EMBED
    if "lm_head" in key:
        return WeightType.LM_HEAD
    return WeightType.NORM


# AWQ (GEMM 版) 存储布局 -> 硬件 MatMul_NT 布局:
# 硬件要求 w_q (N, K) uint4、scale/zp (N, G) (K 连续, 2 元素/字节, 偶数下标低
# nibble, 与 test/operators.py pack_uint4 一致); AWQ 存的是 qweight (K, N/8)
# int32 (一个 int32 沿 N 打 8 个 int4)、qzeros (G, N/8)、scales (G, N),
# 因此 qweight/qzeros 要转置并重打包, scales 要转置。
# lm_head (VOCAB, HIDDEN) 本身就是 (N, K), layernorm 为 1 维, 均无需转换。

# autoawq GEMM 打包沿 N 每 8 个 uint4 交错存放
# (autoawq/modules/linear/gemm.py: iweights = iweights[:, reverse_order])。
# 2026-10-26 实证定案 (_awq_layout_check.py, 服务器 tpu 环境 CPU 前向):
# 不做交错还原时模型输出乱码 (top1='ep'/'rale'); 还原后
# 'The capital of France is' -> 'Paris' 系、'1,2,3,4,5,6,' -> ' 7' (95.9%)。
AWQ_REVERSE_ORDER = (0, 4, 1, 5, 2, 6, 3, 7)


def _unpack_awq_i32(packed: np.ndarray) -> np.ndarray:
    """(rows, cols) int32 -> (rows, cols*8) uint8; 列内 8 个 nibble 低位在前,
    再按 AWQ_REVERSE_ORDER 还原交错 (qweight/qzeros 的 N 维均适用)."""
    u = packed.astype(np.uint32)
    shifts = np.arange(8, dtype=np.uint32) * 4
    nib = (u[:, :, None] >> shifts[None, None, :]) & 0xF
    out = nib.reshape(packed.shape[0], packed.shape[1] * 8).astype(np.uint8)
    idx = np.arange(out.shape[1]).reshape(-1, 8)[:, AWQ_REVERSE_ORDER]
    return out[:, idx.reshape(-1)]


def _pack_u4_pairs(q: np.ndarray) -> np.ndarray:
    """沿最后一维两两打包: 偶数下标低 nibble, 奇数下标高 nibble."""
    return q[:, 0::2] | (q[:, 1::2] << 4)


def convert_qweight(qweight: np.ndarray) -> np.ndarray:
    """AWQ qweight (K, N/8) int32 -> (N, K) uint4, 打包为 (N, K/2) uint8."""
    return _pack_u4_pairs(_unpack_awq_i32(qweight).T)


def convert_qzeros(qzeros: np.ndarray) -> np.ndarray:
    """AWQ qzeros (G, N/8) int32 -> (N, G) uint4, 打包为 (N, G/2) uint8."""
    return _pack_u4_pairs(_unpack_awq_i32(qzeros).T)


def convert_scales(scales: np.ndarray) -> np.ndarray:
    """AWQ scales (G, N) f16 -> (N, G) f16."""
    return scales.T


_CONVERTERS = {
    WeightType.QWEIGHT: convert_qweight,
    WeightType.QZEROS: convert_qzeros,
    WeightType.SCALES: convert_scales,
}


class Llama2:
    # 层内权重: 逻辑名 -> safetensors key 模板 ({idx}=层号)。
    # *_proj 是 W4 权重基名, 实际 key 为 <base>.{qweight,qzeros,scales} 三件套,
    # 统一用 wkey()/w4_keys()/w4_addrs() 取, 不要在别处再拼字符串。
    LAYER_WEIGHTS = {
        "input_norm": "model.layers.{idx}.input_layernorm.weight",
        "q_proj":     "model.layers.{idx}.self_attn.q_proj",
        "k_proj":     "model.layers.{idx}.self_attn.k_proj",
        "v_proj":     "model.layers.{idx}.self_attn.v_proj",
        "o_proj":     "model.layers.{idx}.self_attn.o_proj",
        "post_norm":  "model.layers.{idx}.post_attention_layernorm.weight",
        "gate_proj":  "model.layers.{idx}.mlp.gate_proj",
        "up_proj":    "model.layers.{idx}.mlp.up_proj",
        "down_proj":  "model.layers.{idx}.mlp.down_proj",
    }
    W4_SUFFIXES = ("qweight", "qzeros", "scales")  # 三件套后缀 (装载顺序)
    # 层外权重 (整模型各一份)
    EMBED_KEY = "model.embed_tokens.weight"        # host 侧, 不上 HBM
    FINAL_NORM_KEY = "model.norm.weight"
    LM_HEAD_KEY = "lm_head.weight"

    @classmethod
    def wkey(cls, name: str, idx: int) -> str:
        """层内权重的 safetensors key (逻辑名见 LAYER_WEIGHTS)。"""
        return cls.LAYER_WEIGHTS[name].format(idx=idx)

    @classmethod
    def w4_keys(cls, name: str, idx: int) -> list[str]:
        """W4 投影的 (qweight, qzeros, scales) 三件套 key。"""
        base = cls.wkey(name, idx)
        return [f"{base}.{s}" for s in cls.W4_SUFFIXES]

    def w4_addrs(self, name: str, idx: int) -> list[int]:
        """matmul_w4 的权重输入地址 (qweight, scales, qzeros, 与 spec 输入序一致)。"""
        base = self.wkey(name, idx)
        return [self.addr_of(f"{base}.qweight"),
                self.addr_of(f"{base}.scales"),
                self.addr_of(f"{base}.qzeros")]

    def __init__(self, llm_path: str):
        self.llm_read = LlmRead(llm_path)
        self.llm_config = self.llm_read.config  # HF config.json (无则为 {})
        self.num_layers = 32
        self.seq_length = 256
        self.num_heads = 32
        self.head_dim = 128
        self.hidden_size = self.num_heads * self.head_dim
        # 形状已常量折叠进 IQ (ADR-0002), ckpt config 与烘焙维度不符直接报错
        for key, expect in [("num_hidden_layers", self.num_layers),
                            ("hidden_size", self.hidden_size),
                            ("num_attention_heads", self.num_heads)]:
            if key in self.llm_config and self.llm_config[key] != expect:
                raise ValueError(
                    f"config.{key}={self.llm_config[key]} 与本仓 IQ 烘焙值 {expect} 不符")
        self.embed_tokens = None  # host-side weight, not pushed to HBM
        self.key_specs = {}  # key -> KeySpec (HBM-resident weights only)
        self.cur_pos = 0  # 下一个待写 token 在 KV cache 中的位置 (prefill 结束后 = real_len)
        self._next_addr = 0x0000_0000
        self.u280 = U280()
        self.u280.init_device()
        load_all_iq()
    
    def init_module(self):
        # load weights first
        self.load_weights()
        # allocate K cache, and V cache: 每层一份 (SEQ, HIDDEN) f16
        # (attention 的 K/V 操作数是完整 (SEQ, HEADS, HEAD_DIM), 每层独立寻址)
        self.k_cache_addr = [self._alloc_addr(self.seq_length * self.hidden_size * 2)
                             for _ in range(self.num_layers)]  # f16
        self.v_cache_addr = [self._alloc_addr(self.seq_length * self.hidden_size * 2)
                             for _ in range(self.num_layers)]  # f16
        # allocate embedding word
        self.embed_addr = self._alloc_addr(self.seq_length * self.hidden_size * 2)  # f16
        self.cos_addr = self._alloc_addr(self.seq_length * self.head_dim * 2)  # f16
        self.sin_addr = self._alloc_addr(self.seq_length * self.head_dim * 2)  # f16
        # decode 每步的 mask 行 (1, 1, 1, SEQ) f16, 常驻
        self.mask_row_addr = self._alloc_addr(self.seq_length * 2)  # f16

    def weight_keys(self) -> list[str]:
        keys = []
        for i in range(self.num_layers):
            for name in self.LAYER_WEIGHTS:
                if name.endswith("_proj"):
                    keys.extend(self.w4_keys(name, i))
                else:
                    keys.append(self.wkey(name, i))
        keys.append(self.FINAL_NORM_KEY)
        keys.append(self.LM_HEAD_KEY)
        return keys

    def _alloc_addr(self, nbytes: int) -> int:
        addr = (self._next_addr + ADDR_ALIGN - 1) & ~(ADDR_ALIGN - 1)
        self._next_addr = addr + nbytes
        return addr

    def _alloc_scratch(self):
        """临时 buffer 的 checkpoint/restore 申请方式:
        先用 checkpoint() 记下当前 bump 指针, 申请完后用 free_scratch(ckpt)
        回滚释放。仅对 LIFO 顺序成立 —— 释放时必须保证 ckpt 之后的所有申请
        都不再被使用 (即相关 execute 已完成)。"""
        return self._next_addr

    def _free_scratch(self, checkpoint: int):
        self._next_addr = checkpoint

    def _text_rotary_cos_sin(self):
        # Llama2 的 rope_theta 是 10000.0 (1e6 是 Llama3 的值), 优先取 config
        rope_theta = float(self.llm_config.get("rope_theta", 10000.0))
        inv_freq = 1.0 / (rope_theta
                            **(np.arange(0, self.head_dim, 2, dtype=np.float32) / self.head_dim))
        t = np.arange(self.seq_length, dtype=np.float32)
        freqs = np.outer(t, inv_freq)
        cos = np.cos(freqs).astype(np.float32)
        sin = np.sin(freqs).astype(np.float32)
        cos = np.concatenate([cos, cos], axis=1)
        sin = np.concatenate([sin, sin], axis=1)
        cos = cos.reshape(self.seq_length, 1, self.head_dim)
        sin = sin.reshape(self.seq_length, 1, self.head_dim)
        return cos.astype(np.float16), sin.astype(np.float16)

    def load_weights(self):
        # embed_tokens stays on the host: embedding lookup runs on the host
        self.embed_tokens = self.llm_read.read(self.EMBED_KEY)
        self.cos_embed, self.sin_embed = self._text_rotary_cos_sin()
        # all other weights are pushed to HBM (W4 tensors repacked to NT layout)
        for key in self.weight_keys():
            tensor = self.llm_read.read(key)
            wtype = weight_type_of(key)
            convert = _CONVERTERS.get(wtype)
            if convert is not None:
                tensor = convert(tensor)
            data = tensor.tobytes()
            addr = self._alloc_addr(len(data))
            self.u280.h2c(data, addr, len(data))
            self.key_specs[key] = KeySpec(key, addr, len(data), wtype)
        # ====== OEM weights ============
        # rsqrt magic LUT: 4 个 f16 常数 ×16 lane = 128B, 与 spec 的 (128,) U8
        # 输入一致。内容与 R3 备注单点维护在 iq_map.RSQRT_MAGIC_LUT。
        assert len(RSQRT_MAGIC_LUT) == 128
        self.key_specs["rsqrt_magic"] = KeySpec(
            "rsqrt_magic", self._alloc_addr(len(RSQRT_MAGIC_LUT)),
            len(RSQRT_MAGIC_LUT), WeightType.HOST)
        self.u280.h2c(RSQRT_MAGIC_LUT, self.key_specs["rsqrt_magic"].addr,
                      len(RSQRT_MAGIC_LUT))

    def spec_of(self, key: str) -> KeySpec:
        return self.key_specs[key]

    def addr_of(self, key: str) -> int:
        return self.key_specs[key].addr

    def _rmsnorm(self, phase: str, seq: int, in_addr: int, weight_key: str, out_addr: int):
        # RMSNorm decomposition (需求表 §5): ReduceMean(x²)+eps → Rsqrt → Mul → Mul(weight)
        # 中间 buffer 临时申请, 四个算子全部执行完后立即释放
        ckpt = self._alloc_scratch()
        ms_addr = self._alloc_addr(seq * 2)          # reduce_mean_square out (1, seq, 1) f16
        rsqrt_addr = self._alloc_addr(seq * 2)       # rsqrt out (1, seq, 1) f16
        rms_mul_addr = self._alloc_addr(seq * self.hidden_size * 2)  # mul_rsqrt out (1, seq, HIDDEN) f16
        spec_reduce_mean_square = by_case("reduce_mean_square", phase)
        self.u280.execute(spec_reduce_mean_square.iq, [in_addr], [ms_addr])
        spec_rsqrt = by_case("rsqrt", phase)
        self.u280.execute(spec_rsqrt.iq, [ms_addr, self.addr_of("rsqrt_magic")], [rsqrt_addr])
        spec_mul_rsqrt = by_case("rmsnorm_mul_rsqrt", phase)
        self.u280.execute(spec_mul_rsqrt.iq, [in_addr, rsqrt_addr], [rms_mul_addr])
        spec_mul_weight = by_case("rmsnorm_mul_weight", phase)
        self.u280.execute(spec_mul_weight.iq, [rms_mul_addr, self.addr_of(weight_key)], [out_addr])
        self._free_scratch(ckpt)

    def _rope(self, phase: str, seq: int, in_addr: int, cos_addr: int, sin_addr: int, out_addr: int):
        # RoPE decomposition (需求表 §5.2): rot=RotateHalf(X); Y = X*cos + rot*sin
        # rope_mul_cos 是头维广播 Mul, cos/sin 两次复用; 末步 add 复用 add_f16
        # ((1,seq,HEADS,HEAD_DIM) 与 (1,seq,HIDDEN) 元素数、连续布局一致)。
        # 中间 buffer 临时申请, 全部执行完后立即释放
        ckpt = self._alloc_scratch()
        rot_addr = self._alloc_addr(seq * self.hidden_size * 2)    # rotate_half out (1, seq, HEADS, HEAD_DIM) f16
        mul_cos_addr = self._alloc_addr(seq * self.hidden_size * 2)  # X*cos out f16
        mul_sin_addr = self._alloc_addr(seq * self.hidden_size * 2)  # rot*sin out f16
        spec_rotate_half = by_case("rotate_half", phase)
        self.u280.execute(spec_rotate_half.iq, [in_addr], [rot_addr])
        spec_mul = by_case("rope_mul_cos", phase)
        self.u280.execute(spec_mul.iq, [in_addr, cos_addr], [mul_cos_addr])
        self.u280.execute(spec_mul.iq, [rot_addr, sin_addr], [mul_sin_addr])
        spec_add = by_case("add_f16", phase)
        self.u280.execute(spec_add.iq, [mul_cos_addr, mul_sin_addr], [out_addr])
        self._free_scratch(ckpt)

    def _forward_layer(self, i: int, phase: str, seq: int,
                       xa_addr: int, xb_addr: int, mask_addr: int, row_off: int = 0):
        """单层 Transformer 前向, prefill/decode 同构, 仅序列维与 cache 行偏移不同:
        prefill: seq=SEQ, row_off=0, K/V 写满本层 cache, mask 为 (SEQ, SEQ) 因果 mask
        decode:  seq=1, row_off=cur_pos 行字节偏移, K/V 只写一行, mask 为 (1, SEQ) 行
        hidden state 在 xa/xb 间乒乓 (两次残差交换), 本层中间 buffer 层末统一释放。"""
        H = seq * self.hidden_size * 2                  # (1, seq, HIDDEN) f16 字节数
        A = self.num_heads * seq * self.seq_length * 2  # (1, HEADS, seq, SEQ) f16
        F = seq * FFN * 2                               # (1, seq, FFN) f16
        k_cache_i = self.k_cache_addr[i] + row_off      # 本层 K/V 写入位置
        v_cache_i = self.v_cache_addr[i] + row_off
        k_cache_base = self.k_cache_addr[i]             # attention 读整层 cache
        v_cache_base = self.v_cache_addr[i]
        ckpt = self._alloc_scratch()

        # 1. input rmsnorm
        norm_addr = self._alloc_addr(H)
        self._rmsnorm(phase, seq, xa_addr, self.wkey("input_norm", i), norm_addr)

        # 2. QKV projection (同一个 matmul_w4_qkvo, 换权重调三次)
        spec_qkvo = by_case("matmul_w4_qkvo", phase)
        q_addr = self._alloc_addr(H)
        self.u280.execute(spec_qkvo.iq,
                          [norm_addr, *self.w4_addrs("q_proj", i)], [q_addr])
        k_addr = self._alloc_addr(H)
        self.u280.execute(spec_qkvo.iq,
                          [norm_addr, *self.w4_addrs("k_proj", i)], [k_addr])
        # V 不过 RoPE, matmul 直接写进本层 V cache
        self.u280.execute(spec_qkvo.iq,
                          [norm_addr, *self.w4_addrs("v_proj", i)], [v_cache_i])

        # 3. RoPE on Q/K; K 旋转后直接写进本层 K cache
        q_rope_addr = self._alloc_addr(H)
        self._rope(phase, seq, q_addr, self.cos_addr, self.sin_addr, q_rope_addr)
        self._rope(phase, seq, k_addr, self.cos_addr, self.sin_addr, k_cache_i)

        # 4. attention: QK^T → +mask → softmax → ×V
        scores_addr = self._alloc_addr(A)
        spec_qk = by_case("matmul_qk", phase)
        self.u280.execute(spec_qk.iq, [q_rope_addr, k_cache_base], [scores_addr])
        masked_addr = self._alloc_addr(A)
        spec_mask = by_case("add_mask", phase)
        self.u280.execute(spec_mask.iq, [scores_addr, mask_addr], [masked_addr])
        attn_addr = self._alloc_addr(A)
        spec_softmax = by_case("softmax", phase)
        self.u280.execute(spec_softmax.iq, [masked_addr], [attn_addr])
        ctx_addr = self._alloc_addr(H)
        spec_xv = by_case("matmul_xv", phase)
        self.u280.execute(spec_xv.iq, [attn_addr, v_cache_base], [ctx_addr])

        # 5. O projection + 残差
        o_addr = self._alloc_addr(H)
        self.u280.execute(spec_qkvo.iq,
                          [ctx_addr, *self.w4_addrs("o_proj", i)], [o_addr])
        spec_add = by_case("add_f16", phase)
        self.u280.execute(spec_add.iq, [xa_addr, o_addr], [xb_addr])

        # 6. post rmsnorm
        norm2_addr = self._alloc_addr(H)
        self._rmsnorm(phase, seq, xb_addr, self.wkey("post_norm", i), norm2_addr)

        # 7. FFN: gate/up → SiLU → Mul → down
        spec_gate_up = by_case("matmul_w4_gate_up", phase)
        gate_addr = self._alloc_addr(F)
        self.u280.execute(spec_gate_up.iq,
                          [norm2_addr, *self.w4_addrs("gate_proj", i)], [gate_addr])
        up_addr = self._alloc_addr(F)
        self.u280.execute(spec_gate_up.iq,
                          [norm2_addr, *self.w4_addrs("up_proj", i)], [up_addr])
        silu_addr = self._alloc_addr(F)
        spec_silu = by_case("silu", phase)
        self.u280.execute(spec_silu.iq, [gate_addr], [silu_addr])
        h_addr = self._alloc_addr(F)
        spec_mul_gate = by_case("mul_gate", phase)
        self.u280.execute(spec_mul_gate.iq, [silu_addr, up_addr], [h_addr])
        down_addr = self._alloc_addr(H)
        spec_down = by_case("matmul_w4_down", phase)
        self.u280.execute(spec_down.iq,
                          [h_addr, *self.w4_addrs("down_proj", i)], [down_addr])

        # 8. 残差回 xa, 本层结束
        self.u280.execute(spec_add.iq, [xb_addr, down_addr], [xa_addr])
        self._free_scratch(ckpt)

    def _final_logits(self, phase: str, seq: int, xa_addr: int, last_row: int) -> int:
        """final rmsnorm + lm_head + host greedy argmax; last_row 指定取哪一行 hidden。"""
        H = seq * self.hidden_size * 2
        ckpt = self._alloc_scratch()
        final_norm_addr = self._alloc_addr(H)
        self._rmsnorm(phase, seq, xa_addr, self.FINAL_NORM_KEY, final_norm_addr)
        last_hidden_addr = final_norm_addr + last_row * self.hidden_size * 2
        logits_addr = self._alloc_addr(VOCAB * 4)  # lmhead out (1, VOCAB) f32
        spec_lmhead = by_case("lmhead_matmul_nt_f16", "common")
        self.u280.execute(spec_lmhead.iq,
                          [last_hidden_addr, self.addr_of(self.LM_HEAD_KEY)],
                          [logits_addr])
        logits = np.frombuffer(self.u280.c2h(logits_addr, VOCAB * 4), dtype=np.float32)
        self._free_scratch(ckpt)
        return int(np.argmax(logits))

    def run_prefill(self, input_ids: list) -> int:
        # input_ids 兼容 list / torch tensor
        ids = [int(t) for t in input_ids]
        real_len = len(ids)
        if real_len == 0:
            raise ValueError("run_prefill: 输入为空")
        if real_len > self.seq_length:
            raise ValueError(
                f"run_prefill: prompt 长度 {real_len} 超过 SEQ={self.seq_length} "
                "(prompt+generate 合计上限), 请截断输入")
        # pad input to SEQ
        fix_ids = ids + [0] * (self.seq_length - real_len)
        # causal mask: 仅 real_len×real_len 块内下三角(含对角)为 0, 其余全部 -10000,
        # padding 行/列全部被 mask 掉
        input_mask = np.full((self.seq_length, self.seq_length), -10000.0, dtype=np.float32)
        input_mask[:real_len, :real_len] = np.triu(
            np.full((real_len, real_len), -10000.0, dtype=np.float32), k=1)
        input_mask = input_mask.astype(np.float16)
        position_ids = np.arange(self.seq_length, dtype=np.int32)
        position_ids[real_len:] = 0

        # 1. embedding word tokens
        embed = self.embed_tokens[fix_ids]
        self.u280.h2c(embed.tobytes(), self.embed_addr, embed.nbytes)

        # 2. embedding cos/sin
        cos_embed = self.cos_embed[position_ids]
        self.u280.h2c(cos_embed.tobytes(), self.cos_addr, cos_embed.nbytes)
        sin_embed = self.sin_embed[position_ids]
        self.u280.h2c(sin_embed.tobytes(), self.sin_addr, sin_embed.nbytes)

        # mask/xb 及后续中间 buffer 均为本次 prefill 的临时占用, 结束后整体释放
        ckpt = self._alloc_scratch()

        # 3. causal mask (prefill 内所有层 add_mask 共用)
        mask_addr = self._alloc_addr(input_mask.nbytes)
        self.u280.h2c(input_mask.tobytes(), mask_addr, input_mask.nbytes)

        # hidden state 乒乓: xa 起点是 embedding, 每层两次残差在 xa/xb 间交换,
        # 循环结束后结果回到 xa
        xb_addr = self._alloc_addr(self.seq_length * self.hidden_size * 2)  # f16
        xa_addr = self.embed_addr

        for i in range(self.num_layers):
            self._forward_layer(i, "prefill", self.seq_length, xa_addr, xb_addr, mask_addr)

        # 4. final rmsnorm + lm_head (只取最后一个真实 token 的 hidden)
        token = self._final_logits("prefill", self.seq_length, xa_addr, real_len - 1)
        self._free_scratch(ckpt)

        # 5. 记录 decode 起始位置
        self.cur_pos = real_len
        return token

    def run_decode(self, token: int) -> int:
        # 结构与 run_prefill 同构, 序列维 SEQ → 1; K/V 追加到 cache 的 cur_pos 行
        if self.cur_pos >= self.seq_length:
            raise RuntimeError(
                f"KV cache 已满 (cur_pos={self.cur_pos}, SEQ={self.seq_length}), 无法继续 decode")

        # 1. embedding: 当前 token 单行
        embed = self.embed_tokens[[token]]
        self.u280.h2c(embed.tobytes(), self.embed_addr, embed.nbytes)

        # 2. cos/sin: 当前 position 单行 (1, HEAD_DIM) → spec 的 (1, 1, 1, HEAD_DIM)
        cos_row = self.cos_embed[self.cur_pos]
        self.u280.h2c(cos_row.tobytes(), self.cos_addr, cos_row.nbytes)
        sin_row = self.sin_embed[self.cur_pos]
        self.u280.h2c(sin_row.tobytes(), self.sin_addr, sin_row.nbytes)

        # 3. mask 行 (1, 1, 1, SEQ): 只放开 <= cur_pos 的位置
        mask_row = np.full(self.seq_length, -10000.0, dtype=np.float16)
        mask_row[:self.cur_pos + 1] = 0.0
        self.u280.h2c(mask_row.tobytes(), self.mask_row_addr, mask_row.nbytes)

        # xb 及后续中间 buffer 均为本步 decode 的临时占用, 结束后整体释放
        ckpt = self._alloc_scratch()
        row_off = self.cur_pos * self.hidden_size * 2  # 本步 K/V 在层内的行偏移
        xb_addr = self._alloc_addr(self.hidden_size * 2)  # (1, 1, HIDDEN) f16
        xa_addr = self.embed_addr

        for i in range(self.num_layers):
            self._forward_layer(i, "decode", 1, xa_addr, xb_addr,
                                self.mask_row_addr, row_off)

        # 4. final rmsnorm + lm_head (decode 只有一行)
        next_token = self._final_logits("decode", 1, xa_addr, 0)
        self._free_scratch(ckpt)

        # 5. 推进位置
        self.cur_pos += 1
        return next_token


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--model_path", type=str, required=True)
    parser.add_argument("--max_new_tokens", type=int, default=128,
                        help="单次生成的最大 token 数 (同时受 KV cache 剩余容量限制)")
    args = parser.parse_args()
    llama2 = Llama2(args.model_path)
    llama2.init_module()  # 装载权重 + 分配 KV cache/embed/cos/sin buffer
    tokenizer = AutoTokenizer.from_pretrained(args.model_path)
    while True:
        prompt = input("User: ")
        if prompt.lower() in ["exit", "quit"]:
            break
        input_ids = tokenizer(prompt, return_tensors="pt").input_ids
        print("\nAssistant: \n")
        p_start = time.time()
        token = llama2.run_prefill(input_ids)
        p_end = time.time()
        full_word_tokens = []
        num_tokens = 0
        # 生成上限: max_new_tokens 与 KV cache 剩余容量取小, 防止 run_decode 撞满抛错
        max_gen = min(args.max_new_tokens, llama2.seq_length - llama2.cur_pos)
        while token != tokenizer.eos_token_id and num_tokens < max_gen:
            full_word_tokens.append(token)
            word = tokenizer.decode(full_word_tokens, skip_special_tokens=True)
            num_tokens += 1
            if "�" in word:
                token = llama2.run_decode(token)
                continue
            print(word, end="", flush=True)
            token = llama2.run_decode(token)
            full_word_tokens = []
        end_time = time.time()
        if token != tokenizer.eos_token_id:
            print(f"\n[已达生成长度上限 ({max_gen} tokens), 停止生成]")
        print(f"\nTotal tokens generated: {num_tokens}")
        print(f"TTFT: {p_end - p_start:.2f} seconds")
        tps = num_tokens / (end_time - p_end)
        print(f"TPS: {tps:.2f}")






