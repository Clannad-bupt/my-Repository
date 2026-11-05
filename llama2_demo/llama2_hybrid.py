#!/usr/bin/env python3
"""llama2_hybrid.py — Llama2-7B AWQ 端到端混合运行时 (host CPU + U280 板上).

与 llama2.py 的关系: llama2.py 是全板上形态 (其 u280.execute 调用面已于
10-14 驱动重写时移除, 当前不可运行, 留作终态参考)。本文件是混合形态 —
激活/KV cache 常驻 host numpy, 每算子按 BACKENDS 表 dispatch:

- host: host_ops 的算子 (add_mask / softmax / add_f16 / silu,
  数学同 test/operators.py golden)
- board: 已注册到 RUNNERS 的适配器 (h2c → TB 等价 launch → c2h;
  rsqrt 双 phase / mul 族全 phase / rotate_half 双 phase / lmhead /
  matmul_nt_w4f16 decode / reduce_mean_square 双 phase 由
  board_runners.register_all 接线)。
  matmul 族显式例外 (2026-10-28 端到端首跑, 见 BACKENDS 注释):
  prefill w4 matmul (M=256 发射分钟级/tile) 与 matmul_qk/xv (未交付)
  → host; decode w4 matmul 走板上已位精确 PASS 的交付。

权重: embed_tokens 常驻 host (查表在 host); 各算子的权重输入以 host
numpy 对象形式传入, BoardRunner 侧按对象 id 备忘 DMA (同一对象只
上传一次); 贪心 argmax / tokenizer 在 host。

--dry-run: 无板全链路形状走查 — host 算子真实计算, board 算子按 spec
输出形状返回零张量并记录调用; 校验全部调用点的形状/dtype 契约。

用法:
    python3 llama2_hybrid.py --model_path /home/zwz/models/Llama-2-7B-AWQ \
        [--max_new_tokens 128] [--dry-run]
"""

from __future__ import annotations

import argparse
import os
import time

import numpy as np

import host_ops
import iq_map
from iq_map import HIDDEN, HEADS, HEAD_DIM, SEQ, RSQRT_MAGIC_LUT
from llm_read import LlmRead
from llama2 import convert_qweight, convert_qzeros, convert_scales

NP_F16 = np.float16
NP_F32 = np.float32
_NP_DTYPE = {iq_map.F16: NP_F16, iq_map.F32: NP_F32,
             iq_map.U8: np.uint8, iq_map.I32: np.int32}

# ---------------------------------------------------------------------------
# 后端分配 (2026-11-02 用户定案: prefill 全 host; decode 有 IQ 交付的全 board)
# 背景: slv1 §22/§28 挂组集中在 prefill (rotate_half/reduce_mean/mm_qkvo_m16)
# 与 decode/mm_down, 跨 4 次烧入 (含 1101/1102 两版调试 bit) 身份零变化;
# host_ops 已齐 16/16 (rsqrt 2026-11-02 补齐) —— prefill 整体切 host 全避挂组,
# decode 侧板上已位精确 PASS 的交付全留板上。
# ---------------------------------------------------------------------------

# decode 板上交付 (register_all 已接线): rsqrt / mul 族×4 / rotate_half /
# matmul_w4 qkvo+gate_up / reduce_mean_square (+ common lmhead)。
# 无 IQ 交付恒 host: add_mask / softmax / add_f16 / silu / matmul_qk / matmul_xv。
# decode/matmul_w4_down: 有交付但仍在挂组 (1102 bit 3/3 挂, intid 递增形态),
# 暂 host 例外; 修复 bit 到位后 LLAMA2_MMDOWN=board 切回。
_BOARD_DECODE = frozenset({
    "rsqrt", "rmsnorm_mul_rsqrt", "rmsnorm_mul_weight", "rope_mul_cos",
    "mul_gate", "rotate_half", "reduce_mean_square",
    "matmul_w4_qkvo", "matmul_w4_gate_up",
})

BACKENDS: dict[tuple[str, str], str] = {}
for _s in iq_map.IQ_MAP:
    if _s.case == "sample":
        continue  # 示例条目, 不进 dispatch
    if _s.phase == "prefill":
        _b = "host"   # 2026-11-02 定案: prefill 全 host
    elif _s.phase == "decode":
        _b = "board" if _s.case in _BOARD_DECODE else "host"
    else:
        _b = "board"  # common: lmhead 板上已 PASS (10-24 jabari runner)
    BACKENDS[(_s.phase, _s.case)] = _b

# ---- 环境变量逃生门 ----
if os.environ.get("LLAMA2_MMDOWN") == "board":
    # decode/mm_down 挂组修复后切回板上 (验收: §28 套件该 case 清空)
    BACKENDS[("decode", "matmul_w4_down")] = "board"
if os.environ.get("LLAMA2_MM_DECODE") == "host":
    # decode w4 整体回退 host (无板对拍用)
    for _c in ("matmul_w4_qkvo", "matmul_w4_gate_up", "matmul_w4_down"):
        BACKENDS[("decode", _c)] = "host"
if os.environ.get("LLAMA2_MM_PREFILL") == "board":
    # prefill w4 matmul 上板 (正确性对拍用; 常规 e2e 保持 host)
    for _c in ("matmul_w4_qkvo", "matmul_w4_gate_up", "matmul_w4_down"):
        BACKENDS[("prefill", _c)] = "board"
if os.environ.get("LLAMA2_RM") == "host":
    # reduce_mean 双 phase 回退 (无板对拍/排障)
    BACKENDS[("prefill", "reduce_mean_square")] = "host"
    BACKENDS[("decode", "reduce_mean_square")] = "host"


class RunnerNotWired(RuntimeError):
    """板上算子的适配器尚未注册 (等交付/接线)。"""


# ---------------------------------------------------------------------------
# 板上算子执行器
# ---------------------------------------------------------------------------

class BoardRunner:
    """单个 (phase, case) 的板上执行器协议: run(*inputs) -> out0 ndarray。

    输入/输出为 host numpy 张量 (形状/dtype 遵循 iq_map spec; U4 输入传
    打包后的 uint8, 末维为 spec 逻辑宽度的一半)。实现负责 DMA 往返;
    静态权重输入应利用备忘 (同一对象只 h2c 一次)。
    """

    def run(self, *inputs: np.ndarray) -> np.ndarray:
        raise NotImplementedError


# (phase, case) -> BoardRunner; 各算子族适配器接线后注册到这里。
# TODO(今晚): matmul 交付后, 用其 case_dir + 交付 runner 实现适配器并注册;
# mul 族/rsqrt 包装 test_u280_board.py 的 OPS 框架; lmhead 包装 jabari runner。
RUNNERS: dict[tuple[str, str], BoardRunner] = {}


def register_runner(phase: str, case: str, runner: BoardRunner) -> None:
    RUNNERS[(phase, case)] = runner


# ---------------------------------------------------------------------------
# 混合推理主体
# ---------------------------------------------------------------------------

class Llama2Hybrid:
    LAYER_W4 = ("q_proj", "k_proj", "v_proj", "o_proj",
                "gate_proj", "up_proj", "down_proj")

    def __init__(self, model_path: str, dry_run: bool = False):
        self.dry_run = dry_run
        self.num_layers = 32
        self.seq_length = SEQ
        self.hidden_size = HIDDEN
        self.cur_pos = 0
        self._board_calls = 0
        self._host_calls = 0

        self.llm = LlmRead(model_path)
        cfg = self.llm.config
        for key, expect in [("num_hidden_layers", self.num_layers),
                            ("hidden_size", HIDDEN),
                            ("num_attention_heads", HEADS)]:
            if key in cfg and cfg[key] != expect:
                raise ValueError(
                    f"config.{key}={cfg[key]} 与烘焙值 {expect} 不符")
        self._load_weights()
        self._make_rope_tables()
        # KV cache: (L, K/V, SEQ, HEADS, HEAD_DIM) f16, host 常驻 (~128MB)
        self.kv = np.zeros((self.num_layers, 2, SEQ, HEADS, HEAD_DIM),
                           dtype=NP_F16)
        # rsqrt 的固定 in1 (OEM magic LUT, R3 未验证)
        self.rsqrt_magic = np.frombuffer(RSQRT_MAGIC_LUT, dtype=np.uint8)

    # ---- 装载 ----

    def _load_weights(self) -> None:
        """读 checkpoint; embed 与 norm 留 host; w4 三件套转 NT 布局后留
        host 打包字节 (板上算子的权重输入, DMA 由 runner 备忘)。"""
        t0 = time.time()
        self.embed_tokens = self.llm.read("model.embed_tokens.weight")  # f16
        self.final_norm = self.llm.read("model.norm.weight")            # f16
        self.lm_head = self.llm.read("lm_head.weight")                  # f16
        self.norm_w = {"input_norm": [], "post_norm": []}
        self.w4 = {name: [] for name in self.LAYER_W4}
        for i in range(self.num_layers):
            p = f"model.layers.{i}."
            self.norm_w["input_norm"].append(
                self.llm.read(p + "input_layernorm.weight"))
            self.norm_w["post_norm"].append(
                self.llm.read(p + "post_attention_layernorm.weight"))
            for name in self.LAYER_W4:
                base = p + {"q_proj": "self_attn.q_proj",
                            "k_proj": "self_attn.k_proj",
                            "v_proj": "self_attn.v_proj",
                            "o_proj": "self_attn.o_proj",
                            "gate_proj": "mlp.gate_proj",
                            "up_proj": "mlp.up_proj",
                            "down_proj": "mlp.down_proj"}[name]
                # 与 spec 输入序一致: (qweight, scales, qzeros), NT 布局
                self.w4[name].append((
                    convert_qweight(self.llm.read(base + ".qweight")),
                    convert_scales(self.llm.read(base + ".scales")),
                    convert_qzeros(self.llm.read(base + ".qzeros")),
                ))
        print(f"权重装载完成 ({time.time() - t0:.1f}s)")

    def _make_rope_tables(self) -> None:
        """cos/sin 表 (SEQ, 1, HEAD_DIM) f16; 逻辑同 llama2.py."""
        rope_theta = float(self.llm.config.get("rope_theta", 10000.0))
        inv_freq = 1.0 / (rope_theta ** (
            np.arange(0, HEAD_DIM, 2, dtype=NP_F32) / HEAD_DIM))
        t = np.arange(SEQ, dtype=NP_F32)
        freqs = np.outer(t, inv_freq)
        emb = np.concatenate([np.cos(freqs), np.cos(freqs)], axis=1)
        self.cos_tab = emb.reshape(SEQ, 1, HEAD_DIM).astype(NP_F16)
        emb = np.concatenate([np.sin(freqs), np.sin(freqs)], axis=1)
        self.sin_tab = emb.reshape(SEQ, 1, HEAD_DIM).astype(NP_F16)

    # ---- dispatch ----

    def _op(self, phase: str, case: str, *inputs: np.ndarray) -> np.ndarray:
        """统一算子调用点: 按 BACKENDS 路由; 全部调用点过 spec 形状契约."""
        spec = iq_map.by_case(case, phase)
        self._check_inputs(spec, inputs)
        backend = BACKENDS[(phase, case)]
        if backend == "host":
            self._host_calls += 1
            out = host_ops.DISPATCH[case](*inputs)
        else:
            self._board_calls += 1
            runner = RUNNERS.get((phase, case))
            if runner is None:
                if self.dry_run:
                    out = np.zeros(spec.outputs[0].shape,
                                   dtype=_NP_DTYPE[spec.outputs[0].dtype])
                else:
                    raise RunnerNotWired(
                        f"{phase}/{case} 的板上适配器未注册 "
                        f"(case_dir 交付 + register_runner 接线后可用)")
            else:
                out = runner.run(*inputs)
        self._check_output(spec, out)
        return out

    @staticmethod
    def _check_inputs(spec, inputs) -> None:
        if len(inputs) != len(spec.inputs):
            raise ValueError(
                f"{spec.phase}/{spec.case} 输入数 {len(inputs)} "
                f"!= spec {len(spec.inputs)}")
        for t, arr in zip(spec.inputs, inputs):
            want = t.shape
            got = arr.shape
            if t.dtype == iq_map.U4:
                # U4 逻辑视图 (N,K) 的物理传输为打包 uint8 (N,K/2)
                want = t.shape[:-1] + (t.shape[-1] // 2,)
            if got != want:
                raise ValueError(
                    f"{spec.phase}/{spec.case}.{t.slot} 形状 {got} "
                    f"!= spec {t.shape} ({t.dtype})")

    @staticmethod
    def _check_output(spec, out: np.ndarray) -> None:
        want = spec.outputs[0]
        if out.shape != want.shape:
            raise ValueError(
                f"{spec.phase}/{spec.case} 输出 {out.shape} "
                f"!= spec {want.shape}")

    # ---- 流程 ----

    def _rmsnorm(self, phase: str, x: np.ndarray, weight: np.ndarray):
        ms = self._op(phase, "reduce_mean_square", x)
        r = self._op(phase, "rsqrt", ms, self.rsqrt_magic)
        xr = self._op(phase, "rmsnorm_mul_rsqrt", x, r)
        w = weight.reshape(1, 1, HIDDEN).astype(NP_F16)
        return self._op(phase, "rmsnorm_mul_weight", xr, w)

    def _rope(self, phase: str, x: np.ndarray, cos: np.ndarray,
              sin: np.ndarray) -> np.ndarray:
        seq = x.shape[1]
        xh = x.reshape(1, seq, HEADS, HEAD_DIM)
        rot = self._op(phase, "rotate_half", xh)
        mc = self._op(phase, "rope_mul_cos", xh, cos)
        ms_ = self._op(phase, "rope_mul_cos", rot, sin)
        # add_f16 登记形状为 (1,seq,HIDDEN); (1,seq,HEADS,HEAD_DIM) 与其
        # 元素数/连续布局一致, 按登记形状重视图后复用同一份 IQ/host 实现
        added = self._op(phase, "add_f16",
                         mc.reshape(1, seq, HIDDEN), ms_.reshape(1, seq, HIDDEN))
        return added.reshape(1, seq, HEADS, HEAD_DIM)

    def _matmul_w4(self, phase: str, case: str, x: np.ndarray,
                   name: str, layer: int) -> np.ndarray:
        qw, sc, qz = self.w4[name][layer]
        return self._op(phase, case, x, qw, sc, qz)

    def _forward_layer(self, i: int, phase: str, x: np.ndarray,
                       mask: np.ndarray, pos: slice) -> np.ndarray:
        """单层前向; x: (1, seq, HIDDEN) f16。prefill/decode 同构, 仅 seq
        维与 KV 写入位置不同 (prefill pos=全槽, decode pos=cur_pos 单行)。"""
        # 1. input rmsnorm
        norm = self._rmsnorm(phase, x, self.norm_w["input_norm"][i])

        # 2. QKV 投影
        q = self._matmul_w4(phase, "matmul_w4_qkvo", norm, "q_proj", i)
        k = self._matmul_w4(phase, "matmul_w4_qkvo", norm, "k_proj", i)
        v = self._matmul_w4(phase, "matmul_w4_qkvo", norm, "v_proj", i)

        # 3. RoPE on Q/K (cos/sin 已按本 phase 的位置切片好传入 _rope 前)
        seq = x.shape[1]
        if phase == "prefill":
            position_ids = np.arange(SEQ, dtype=np.int64)
            position_ids[self._real_len:] = 0
            cos = self.cos_tab[position_ids].reshape(1, SEQ, 1, HEAD_DIM)
            sin = self.sin_tab[position_ids].reshape(1, SEQ, 1, HEAD_DIM)
        else:
            cos = self.cos_tab[self.cur_pos].reshape(1, 1, 1, HEAD_DIM)
            sin = self.sin_tab[self.cur_pos].reshape(1, 1, 1, HEAD_DIM)
        q = self._rope(phase, q, cos, sin).reshape(1, seq, HIDDEN)
        k = self._rope(phase, k, cos, sin)

        # K/V 写入本层 cache (V 不过 RoPE)
        self.kv[i, 0, pos] = k.reshape(seq, HEADS, HEAD_DIM)
        self.kv[i, 1, pos] = v.reshape(seq, HEADS, HEAD_DIM)

        # 4. attention: QK^T → +mask → softmax → ×V (读整层 cache)
        k_all = self.kv[i, 0][None]          # (1, SEQ, HEADS, HEAD_DIM)
        v_all = self.kv[i, 1][None]
        q4 = q.reshape(1, seq, HEADS, HEAD_DIM)
        scores = self._op(phase, "matmul_qk", q4, k_all)
        masked = self._op(phase, "add_mask", scores, mask)
        attn = self._op(phase, "softmax", masked)
        ctx = self._op(phase, "matmul_xv", attn, v_all)
        ctx = ctx.reshape(1, seq, HIDDEN)

        # 5. O 投影 + 残差
        o = self._matmul_w4(phase, "matmul_w4_qkvo", ctx, "o_proj", i)
        x = self._op(phase, "add_f16", x, o)

        # 6. post rmsnorm + FFN
        norm2 = self._rmsnorm(phase, x, self.norm_w["post_norm"][i])
        g = self._matmul_w4(phase, "matmul_w4_gate_up", norm2, "gate_proj", i)
        u = self._matmul_w4(phase, "matmul_w4_gate_up", norm2, "up_proj", i)
        s = self._op(phase, "silu", g)
        h = self._op(phase, "mul_gate", s, u)
        d = self._matmul_w4(phase, "matmul_w4_down", h, "down_proj", i)
        return self._op(phase, "add_f16", x, d)

    @staticmethod
    def _causal_mask(real_len: int) -> np.ndarray:
        """prefill mask (1,1,SEQ,SEQ): real_len 块内下三角(含对角)允许,
        其余 -3e4 (与 golden 用例 _causal_mask 同约定)。"""
        mask = np.full((1, 1, SEQ, SEQ), -3.0e4, dtype=NP_F16)
        allowed = np.arange(SEQ)[:, None] >= np.arange(SEQ)[None, :]
        block = np.zeros((SEQ, SEQ), dtype=bool)
        block[:real_len, :real_len] = allowed[:real_len, :real_len]
        mask[..., block] = 0.0
        return mask

    def _final_token(self, phase: str, x: np.ndarray, last_row: int) -> int:
        norm = self._rmsnorm(phase, x, self.final_norm)
        last = norm[:, last_row:last_row + 1, :].reshape(1, HIDDEN)
        logits = self._op("common", "lmhead_matmul_nt_f16", last, self.lm_head)
        self.last_logits = logits  # 对拍/诊断用 (32000,) f32, 见 _e2e_awq_check
        return int(np.argmax(logits.astype(NP_F32)))

    def run_prefill(self, input_ids) -> int:
        ids = [int(t) for t in input_ids]
        real_len = len(ids)
        if not 0 < real_len <= SEQ:
            raise ValueError(
                f"prompt 长度 {real_len} 超出上限 SEQ={SEQ} 或为空")
        self._real_len = real_len
        self.cur_pos = real_len
        fix_ids = ids + [0] * (SEQ - real_len)
        x = self.embed_tokens[fix_ids][None].astype(NP_F16)  # (1,SEQ,HIDDEN)
        mask = self._causal_mask(real_len)
        for i in range(self.num_layers):
            x = self._forward_layer(i, "prefill", x, mask,
                                    pos=slice(0, SEQ))
        return self._final_token("prefill", x, real_len - 1)

    def run_decode(self, token: int) -> int:
        if self.cur_pos >= SEQ:
            raise RuntimeError(f"KV cache 已满 (SEQ={SEQ})")
        x = self.embed_tokens[[token]][None].astype(NP_F16)  # (1,1,HIDDEN)
        mask = np.full((1, 1, 1, SEQ), -3.0e4, dtype=NP_F16)
        mask[..., :self.cur_pos + 1] = 0.0
        for i in range(self.num_layers):
            x = self._forward_layer(i, "decode", x, mask,
                                    pos=slice(self.cur_pos, self.cur_pos + 1))
        nxt = self._final_token("decode", x, 0)
        self.cur_pos += 1
        return nxt


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model_path", type=str, required=True)
    parser.add_argument("--max_new_tokens", type=int, default=128)
    parser.add_argument("--dry-run", action="store_true",
                        help="无板形状走查: host 算子真实计算, board 算子"
                             "按 spec 返回零张量并记录调用")
    parser.add_argument("--prefill-dual6", action="store_true",
                        help="mul prefill 用 dual6 6-bank 双 tile 变体 "
                             "(stage 数减半; rope_mul_cos 无 dual6 自动"
                             "回退常规)")
    args = parser.parse_args()

    llama2 = Llama2Hybrid(args.model_path, dry_run=args.dry_run)
    if args.dry_run:
        from transformers import AutoTokenizer
        tok = AutoTokenizer.from_pretrained(args.model_path)
        # 服务器 python3 的 transformers 为 tokenizer-only (无 torch),
        # 不能用 return_tensors="pt"
        ids = tok.encode("The capital of France is")
        t0 = time.time()
        token = llama2.run_prefill(ids)
        for _ in range(2):
            token = llama2.run_decode(token)
        print(f"dry-run 完成: host 调用 {llama2._host_calls} 次, "
              f"board 调用 {llama2._board_calls} 次, "
              f"{time.time() - t0:.1f}s")
        return

    from transformers import AutoTokenizer
    from u280 import U280
    import board_runners
    tokenizer = AutoTokenizer.from_pretrained(args.model_path)
    # 板卡访问需 sudo 运行本脚本 (与单算子板测一致)
    with U280(backend="xdma", timeout_s=30.0) as u:
        u.reset_smi()
        board_runners.register_all(u, RUNNERS,
                                   prefill_dual6=args.prefill_dual6)
        board_runners.warmup(RUNNERS, llama2.lm_head)  # W 256MB 一次性上传
        while True:
            prompt = input("User: ")
            if prompt.lower() in ["exit", "quit"]:
                break
            input_ids = tokenizer.encode(prompt)  # 无 torch 环境, 不用 pt 张量
            print("\nAssistant: \n")
            p_start = time.time()
            token = llama2.run_prefill(input_ids)
            p_end = time.time()
            full_word_tokens = []
            num_tokens = 0
            max_gen = min(args.max_new_tokens,
                          llama2.seq_length - llama2.cur_pos)
            while token != tokenizer.eos_token_id and num_tokens < max_gen:
                full_word_tokens.append(token)
                word = tokenizer.decode(full_word_tokens,
                                        skip_special_tokens=True)
                num_tokens += 1
                if "" in word:
                    token = llama2.run_decode(token)
                    continue
                print(word, end="", flush=True)
                full_word_tokens = []
                token = llama2.run_decode(token)
            end_time = time.time()
            if token != tokenizer.eos_token_id:
                print(f"\n[已达生成长度上限 ({max_gen} tokens), 停止生成]")
            print(f"\nTotal tokens generated: {num_tokens}")
            print(f"TTFT: {p_end - p_start:.2f} seconds")
            print(f"TPS: {num_tokens / (end_time - p_end):.2f}")


if __name__ == "__main__":
    main()
