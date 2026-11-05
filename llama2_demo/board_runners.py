#!/usr/bin/env python3
"""board_runners.py — llama2_hybrid 混合运行时的板上算子适配器.

每个 Runner 实现 run(*host 张量) -> out0 host 张量 (形状/dtype 遵循
iq_map spec), 内部做 HBM 落盘 (交付原样布局, 不 rebase) + launch +
读回重组。落盘/读回映射直接复用各算子族已位精确 PASS 的测试框架
函数 (test_u280_board.py / test_u280_lmhead_board.py), 不在此重新推导。

覆盖 (2026-10-27):
    rsqrt prefill/decode   RsqrtRunner      (3 stage 单发)
    mul 族 decode x4       MulDecodeRunner  (单发位精确 PASS)
    mul 族 prefill x4      MulPrefillRunner (10-27 修复交付, loops=0
                             store; IQ/ 下常规版 + dual6 6-bank 双
                             tile (stage 减半) 双变体, --prefill-dual6
                             或环境变量 MUL_PREFILL_DUAL6=1 选变体)
    lmhead common          LmheadRunner     (500 轮 tiled; W 一次上传)
    rotate_half 双 phase   RotateRunner     (10-27 IQ 修复, 10-28 位精确)
    matmul_w4 decode x3    MatmulW4Runner   (10-27 交付位精确 PASS)
    reduce_mean 双 phase   ReduceMeanRunner (rm_fixed2 10-29 交付;
                             Σ8 lane − 7·eps 布局定案后接线)
    matmul_w4 prefill x3   PrefillMatmulW4Runner (_m16 store 修复变体;
                             全 N 发射分钟级/tile, 供对拍, 默认 host)

mul 族 prefill 已于 2026-10-27 从 host 兜底切回 board: 旧交付的 store
轮边界假象 (每轮丢 16 元素) 由 何佳宝 10-27 包修复 (store STBAXI 全部
loops=0, 每 tile 16 条单段指令), 板上 11/11 包位精确 PASS
(_mul_1027_board_test.py)。**所有 launch 统一走 TB 等价流**
(_launch_case_tb): 每个 case 首 stage 无条件 cfg_rst 软复位做 case 间
清场 —— 不做则上一 case 的 store 段残留 SMI 队列/地址发生状态, 下一
case load 段错位读陈旧 HBM 且垃圾自我传播 (10-27 _rsqrt_probe2 实证:
同 runner 二跑必挂, rst 后位精确); 每 stage 前另写 DONE=0 预清防假
完成; 多 tile prefill 再加每 L-C-S 3 stage 边界 cfg_rst 防 tile 头
错位 (_launch_case_tb 注释见下)。decode 单发 (3 stage) 边界不触发,
lmhead 轮结构非 3 整除传 tile_rst=False (每 token 仅付首复位 10ms)。
发射慢的主因是 packer beat 重写 (~30us/beat, matmul 25072 beat/launch
≈ 0.6s, 占 97%; 单 stage 硬件执行 <1ms) —— register_all 末尾用
_install_fast_packer 去逐次排序读回压到 ~8us/beat, 协议校验全保留
(2026-10-28 解剖 + selftest 门控)。

rsqrt 常量表注意: iq_map.RSQRT_MAGIC_LUT 是 128B/16-lane 外推版 (R3),
板上实证 (test_u280_rsqrt_rotate_board.py 2026-10-25) 正确内容为
**64B (4 词 x 8 lane)**; 交付 IQ 的 magic 流 base=0x10000000 intlv=0,
即全 SMC 共享同一窗, 单份填充即可 (10-25 的 8-bank 平铺属冗余, 只有
bank1 这份被读)。**Runner 每次 run 重填此表** —— mul 族 in1 区
(weight row_tile 16KB 等) 与表同起 0x10000000, 推理交错会被冲掉。
Runner 内部用实证版, 忽略 spec in1 内容 (其槽位仅作契约占位)。
另: 位技巧在 f16 次正规输入 (<2^-14) 下失效 —— 2026-10-28 e2e
层0 ms 实证 (cos=-0.71 根因), run 内 _rsqrt_prescale 4^k 抬升修复。

接线 (llama2_hybrid main 内):
    with U280(backend="xdma") as u:
        u.reset_smi()
        board_runners.register_all(u, llama2_hybrid.RUNNERS)
        board_runners.warmup(llama2_hybrid.RUNNERS, llama2.lm_head)

自测 (需板卡 + sudo; 各 runner 随机数据对 numpy golden):
    python3 board_runners.py --selftest [--prefill-dual6]

matmul_nt_w4f16 decode x3 MatmulW4Runner (10-27 交付, 3 case x 3
seeds 位精确 PASS; M=1 GEMV x N/32 tile 循环, 2026-10-28 接线)。
matmul_nt_w4f16 prefill x3 PrefillMatmulW4Runner (10-28 交付 M=256;
原 store 真机死锁, 10-29 定案后取 _m16 修复变体, 3x3 seeds PASS;
全 N = 分钟级/tile × 128/344/128 发, 常规 e2e 仍走 host, 见
llama2_hybrid.py BACKENDS 注释)。matmul_qk/xv (王俊驰) 未交付 ——
host 兜底。
reduce_mean_square 双 phase ReduceMeanRunner (rm_fixed2 10-29 交付,
meta_v3 新格式; 布局 host 取证定案: 行 = val0/val1 2KB 交错, 输出
恒等行序, ms = Σ8 f32 lane − 7·eps; c_eps/c_inv/echo 每 run 重写)。
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
NUM_SMC = 8

# ---------------------------------------------------------------------------
# TB 等价 launch (mul 族 prefill 必需; 依据见文件头)
# ---------------------------------------------------------------------------

_CSR_CFG, _CSR_INT, _CSR_DONE = 0x000, 0x004, 0x024
_TB_SETTLE = 0.01   # cfg_rst 后 settle 秒 (与交付 runner 同)
# 2026-10-29: 板子 10-28 晚 wedge + 03:46 重烧后出现非确定 case 间污染
# (selftest 两轮失败搬家/量级漂移; 单点连跑 5/5 干净)。怀疑 cfg_rst 清场
# 边际失效 (dmesg 首现 bmdrv_api_clear_lib sync api error)。对策试验档:
# LLAMA2_TB_GUARD=1 时每复位点双重 cfg_rst + settle 提至 50ms。
_TB_GUARD = bool(os.environ.get("LLAMA2_TB_GUARD"))


def _launch_case_tb(u, handle: int, settle: float = _TB_SETTLE,
                    tile_rst: bool = True) -> list:
    """逐 stage 装载执行, 复刻 RTL TB / 交付 runner 时序.

    差异点 vs launch_case:
      * **s_idx==0 无条件 cfg_rst (CFG[1] 软复位)** —— case 间清场。
        上一 case 的 store 段跑完后 SMI 队列/地址发生状态有残留, 下一
        case 的 load 段不复位必错位 (读陈旧 HBM, 垃圾自我传播到之后
        所有 case; 2026-10-27 _rsqrt_probe2 板上实证: 同 runner 二跑
        无 rst 必挂 err~46, 每 stage 前 cfg_rst 或 case 前 reset_smi
        则位精确 PASS)。cfg_rst 只清 SMI 控制/队列状态 (启动队列/
        done 位/忙位/IQ_EN 总线/DONE), 不清 UR/HBM;
      * 每 stage 提交前写 CSR_DONE=0 —— smi_iq_done_r 直赋非 W1C, 只在
        下一批 pop 时由 RTL 清; 不预清则本轮 START 后立即可读到上一批
        的 DONE=1, 判完成脱同步 (交付 runner write_iq_en_and_start 注释,
        smi_ctrl_regs.v:340; 主机写 0 已实证有效, 与 cfg_rst 互补);
      * tile_rst=True 时每 3 stage (一轮 L-C-S) 边界 (idx!=0, %3==0)
        额外 cfg_rst —— loops=0 只修单 tile 内部, 多 tile 流水线 queue
        状态跨 tile 需清, 否则 tile 头写错位 (10-27 板上实证: 不清则
        tile1 窗头起全挂)。单 tile case (3 stage) 边界不触发; lmhead
        等非 3 整除结构传 tile_rst=False, 只取 case 首复位 + DONE 预清。

    **beats 不可跨 launch 复用 (2026-10-28 实证)**: 曾试"同 handle 连发
    时跳过 packer 重写"(matmul 每 launch 25072 beat ≈ 0.6s, 占发射
    97%), selftest 2-tile 抽检 tile1 立刻全挂 (3 case max|err|
    0.68-1.18)。机理 (二选一, 未进一步区分): stage 间 beats 是 IQ 窗
    滑动复用 (后 stage 覆盖前 stage 地址, 全 case 程序不同时驻留), 或
    cfg_rst 连 IQ RAM 一起清。无论哪种, 每 launch 全量重写是必要
    条件, 不要重试该"优化"。发射慢的真正出口在 packer 单 beat 成本
    (~30us, u280._packer_write_beat 每写一次都带排序读回; 对策见
    _install_fast_packer)。
    """
    ent = u._case_entry(handle)
    counts = []
    # beat hex -> 小端字节串缓存: fromhex+反转每 launch 重复做 ~1us/beat
    # (matmul 25072 beat ≈ 25-38ms/launch); case 条目只增不改, 安全。
    key = (id(u), handle)
    stages = _BEAT_BYTES_CACHE.get(key)
    if stages is None:
        stages = [[(b.addr, b.mask, bytes.fromhex(b.data_hex)[::-1])
                   for b in st.beats] for st in ent.stages]
        _BEAT_BYTES_CACHE[key] = stages
    # 优化① (2026-11-01): slv1 run 计划缓存 —— beats 的 A.4 合并结果只依
    # 赖 case 内容与 _slv1_chunk, store 一次预算, 发射时置 _slv1_preplan
    # 由 _slv1_flush_iq 直推 (写计划与逐 beat 路径逐字节相同; 省每 launch
    # 1.73us/beat 拼装 + 合并开销, matmul 2.5 万 beat/launch 量级显著).
    plans = None
    if u._slv1 and not os.environ.get("LLAMA2_SLV1_NO_PLANCACHE"):
        pkey = (id(u), handle, u._slv1_chunk)
        plans = _SLV1_PLAN_CACHE.get(pkey)
        if plans is None:
            plans = [_slv1_stage_plans(beats_, u._slv1_chunk)
                     for beats_ in stages]
            _SLV1_PLAN_CACHE[pkey] = plans
    for s_idx, (st, beats) in enumerate(zip(ent.stages, stages)):
        if s_idx == 0 or (tile_rst and s_idx % 3 == 0):
            u._bar_write32(_CSR_CFG, 0x2)
            time.sleep(settle)
            if _TB_GUARD:
                # 双重复位 + 长 settle: 10-29 板态边际期对策 (见上)
                u._bar_write32(_CSR_CFG, 0x2)
                time.sleep(max(settle, 0.05))
            cfg = u._bar_read32(_CSR_CFG)
            intw = u._bar_read32(_CSR_INT)
            dn = u._bar_read32(_CSR_DONE)
            if cfg != 0 or intw != 0 or dn != 0:
                raise RuntimeError(
                    "stage%d cfg_rst 未生效: CFG=%#x INT=%#x "
                    "DONE=%#x" % (s_idx, cfg, intw, dn))
        if plans is not None:
            u._slv1_preplan = (0, plans[s_idx])
        else:
            for addr, mask, data in beats:
                u._packer_write_beat(addr, mask, data)
        u._bar_write32(_CSR_DONE, 0)
        u._start_and_wait(st.iq_mask, ent.subq)
        counts.append(len(beats))
    return counts


_BEAT_BYTES_CACHE: dict = {}
_SLV1_PLAN_CACHE: dict = {}


def _slv1_stage_plans(beats: list, chunk: int) -> list:
    """一个 stage 的 beats -> A.4 合并 run 计划 [[start, bytes], ...].

    与 u280._slv1_flush_iq 的 img+合并路径语义相同 (合并仅前向相邻段、
    封顶 chunk), 产出由 _slv1_preplan 直推; 恒等性由 _slv1_plan_dryrun.py
    逐字节对拍。
    """
    img = []
    for addr, mask, data in beats:
        lo = (mask & -mask).bit_length() - 1
        hi = mask.bit_length() - 1
        if mask != (((1 << (hi - lo + 1)) - 1) << lo):
            raise ValueError(f"slv1 流推不支持非连续 WSTRB: 0x{mask:04x}")
        img.append((addr + lo, data[lo:hi + 1]))
    runs = []
    for start, payload in img:
        if (runs and start == runs[-1][0] + len(runs[-1][1])
                and len(runs[-1][1]) + len(payload) <= chunk):
            runs[-1][1] += payload
        else:
            runs.append([start, payload])
    return runs


def _install_fast_packer(u) -> None:
    """packer 快写: 替换 u._packer_write_beat, 去掉每次 _bar_write32 自带
    的排序读回 (~1.4us PCIe 往返 x 9 次/beat ≈ 占 30us/beat 的大头;
    2026-10-28 解剖: matmul 每 launch 25072 beat, launch 耗时 97% 在
    beat 重写)。PCIe 同设备 posted-write TLP 保序送达, 且随后的 STATUS
    读本身就是全部前序写的冲刷点, 逐次读回冗余。协议校验 (对齐/WSTRB/
    stage_valid==0x3F/commit 计数 +1 + READY) 全部保留 —— 丢写乱写会以
    U280Error 显式暴露, 不会静默错数据。mock 后端不替换 (保留
    _mock_packer_log 语义)。环境变量 U280_NO_FAST_PACKER=1 可关。
    """
    if u.backend == "mock" or os.environ.get("U280_NO_FAST_PACKER") == "1":
        return
    if getattr(u, "_slv1", False):
        # slv1 (20261030+ bit): packer 已从 RTL 删除, u._packer_write_beat
        # 已是 SGDMA 直推 (无 BAR 可 mmap) —— 没有可加速的协议层, 不替换.
        return
    import struct as _st
    from u280 import (IQ_WINDOW_BASE, PACKER_CTRL, PACKER_CTRL_CLEAR_ERROR,
                      PACKER_CTRL_CLEAR_STAGING, PACKER_CTRL_COMMIT,
                      PACKER_DATA, PACKER_IQADDR, PACKER_STATUS,
                      PACKER_STATUS_ANY_ERROR,
                      PACKER_STATUS_COMMIT_COUNT_SHIFT,
                      PACKER_STATUS_STAGE_VALID_SHIFT, PACKER_WSTRB,
                      U280Error)

    def fast_write_beat(addr: int, beat_mask: int, data: bytes) -> None:
        if addr & 0xF:
            raise U280Error(f"IQ packer 目标未 16B 对齐: 0x{addr:08x}")
        if addr < IQ_WINDOW_BASE or addr >= 0x5000:
            raise U280Error(f"IQ packer 目标超出 IQ 窗口: 0x{addr:08x}")
        if beat_mask == 0 or beat_mask > 0xFFFF:
            raise U280Error(f"IQ packer WSTRB 非法: 0x{beat_mask:x}")
        if len(data) != 16:
            raise U280Error(f"IQ DATA 不是 16B: {data.hex()}")
        mm = u._bar_mm
        assert mm is not None
        # 7 次 staging 写: 纯 posted write, 无逐次排序读回
        _st.pack_into("<I", mm, PACKER_CTRL,
                      PACKER_CTRL_CLEAR_ERROR | PACKER_CTRL_CLEAR_STAGING)
        _st.pack_into("<4I", mm, PACKER_DATA[0], *_st.unpack("<4I", data))
        _st.pack_into("<I", mm, PACKER_WSTRB, beat_mask)
        _st.pack_into("<I", mm, PACKER_IQADDR, addr)
        # STATUS 读 = 校验 + 前序写的冲刷点 (与原实现同一判定)
        status = _st.unpack_from("<I", mm, PACKER_STATUS)[0]
        if status & PACKER_STATUS_ANY_ERROR:
            raise U280Error(
                f"IQ packer pre-commit 错误: status=0x{status:08x}")
        stage_valid = (status >> PACKER_STATUS_STAGE_VALID_SHIFT) & 0x3F
        if stage_valid != 0x3F:
            raise U280Error(
                f"IQ packer staging 不完整: status=0x{status:08x} "
                f"stage_valid=0x{stage_valid:02x}")
        _st.pack_into("<I", mm, PACKER_CTRL, PACKER_CTRL_COMMIT)
        u._packer_wait_commit(
            (status >> PACKER_STATUS_COMMIT_COUNT_SHIFT) & 0xFFFF)

    u._packer_write_beat = fast_write_beat  # type: ignore[method-assign]


# ---------------------------------------------------------------------------
# rsqrt (prefill/decode 同构; 区域常量来自 10-24 store_case 推导 + 10-25 实证)
# ---------------------------------------------------------------------------

# 板上实证的常量表: 4 个 f16 词各 8 lane = 64B (非 iq_map 的 128B R3 版)
_RSQRT_LUT64 = b"".join(
    w.to_bytes(2, "little") * 8 for w in (0x3E00, 0x3800, 0x0001, 0x59BB))

_RS_IN0_BASE, _RS_MAGIC_STRIDE = 0x1000, 0x10000000
_RS_OUT_BASE, _RS_OUT_INTLV = 0x2000, 0x10000000
_RS_WIN = 0x100  # 每 SMC 一窗 128 lane f16 = 256B
_F16_MIN_NORM = float(np.finfo(np.float16).tiny)  # 2^-14 = 6.1035e-5


def _rsqrt_prescale(vals: np.ndarray):
    """次正规抬升: (缩后 f16 输入, 输出还原乘子 2^k) —— 2026-10-28 实证.

    板上 rsqrt = f16 域位技巧 (0x59BB 魔数 + 牛顿迭代), 输入落 f16
    次正规区 (< 2^-14) 时浮点位解释失效: e2e 层0 ms 实测 1.0e-5
    (pad 行, reduce_mean_square 的 EPS 底) 与 4.15e-5 (逗号行,
    每 3 行一个, 含末行 18), 板出 r 偏大 2-3x (471.75 vs 155.25),
    真实行 K/V 被过缩放污染全注意力 -> e2e prefill cos=-0.7073
    (_prefill_spy 全 prefill 仅层0 两次 rsqrt 超阈, err 316.5;
    _rsqrt_ms_probe 逐位置定案)。层1+ hidden 累积后 ms 回正常区,
    故 selftest 均匀分布 [0.5,2] 与深层均从未暴露 —— 验证域盲区。
    修复: 逐行预乘 4^k 抬进正常区, 输出乘 2^k 还原。2 的幂缩放
    对位技巧严格无损 (次正规 f16 尾数 <=10bit, 抬升后正规表示精确;
    输出还原为指数平移), 探针全值域相对误差 <= 1e-3 (板固有精度)。
    """
    v32 = vals.astype(np.float32)
    k = np.maximum(0, np.ceil(
        np.log(_F16_MIN_NORM / np.maximum(v32, 1e-45))
        / np.log(4.0))).astype(np.int64)
    if not k.any():
        return vals, None
    return (v32 * (4.0 ** k)).astype("<f2"), (2.0 ** k).astype(np.float32)


class RsqrtRunner:
    """in0 (1,n,1) f16 (n=1 decode / 256 prefill) -> out0 同形.

    填充 (rsqrt_fill verify 路径实证): decode 标量平铺 128 lane;
    prefill 256 值按 SMC 32 个均分, 片内 x4 平铺满窗。
    读回: 有效 lane decode 8/SMC (取 lane0), prefill 32/SMC。
    """

    def __init__(self, u, case_dir, n: int):
        self.u = u
        self.n = n
        self.handle = u.store_case(case_dir)

    @staticmethod
    def fill_lut(u) -> None:
        """填充 magic LUT (4 词 x 8 lane, 共享 base=0x10000000).

        对照 test_u280_rsqrt_rotate_board.py rsqrt_regions 的 magic
        流定义 (intlv=0, 各 SMC 读此一处)。
        """
        u.h2c(_RSQRT_LUT64, _RS_MAGIC_STRIDE, len(_RSQRT_LUT64))

    def run(self, x: np.ndarray, magic: np.ndarray) -> np.ndarray:
        vals = np.ascontiguousarray(x, dtype="<f2").reshape(-1)
        if len(vals) != self.n:
            raise ValueError(f"rsqrt 输入 {len(vals)} != 登记 {self.n}")
        # 次正规抬升 (2026-10-28 定案, 详见 _rsqrt_prescale 注释):
        # 位技巧在 f16 次正规输入下失效, 真实层0 ms 正落此区。
        vals, restore = _rsqrt_prescale(vals)
        # LUT 每次 run 重填: mul 族 in1 区 (weight row_tile 16KB 等) 与
        # magic 表同在 0x10000000 起, 推理交错时会被 mul 填充冲掉;
        # 64B h2c 代价可忽略, 逐次重填一劳永逸。
        self.fill_lut(self.u)
        if self.n == 1:
            per_smc = np.tile(vals, (NUM_SMC, 128))
        else:
            per_smc = np.tile(vals.reshape(NUM_SMC, 32), (1, 4))
        for k in range(NUM_SMC):
            payload = per_smc[k].astype("<f2").tobytes()
            self.u.h2c(payload, _RS_IN0_BASE + k * _RS_MAGIC_STRIDE,
                       len(payload))
        # TB launch: 首 stage cfg_rst 做 case 间清场 (不带则同 runner
        # 二跑必挂, _rsqrt_probe2 实证) + 每 stage DONE 预清
        _launch_case_tb(self.u, self.handle)
        if self.n == 1:
            raw = self.u.c2h(_RS_OUT_BASE, _RS_WIN)  # SMC0 窗
            out = float(np.frombuffer(raw, dtype="<f2")[0])
            if restore is not None:
                out *= float(restore[0])
            return np.array([[[out]]], dtype=np.float16)
        got = np.zeros((NUM_SMC, 128), dtype="<f2")
        for k in range(NUM_SMC):
            raw = self.u.c2h(_RS_OUT_BASE + k * _RS_OUT_INTLV, _RS_WIN)
            got[k] = np.frombuffer(raw, dtype="<f2")
        out = got[:, :32].reshape(1, self.n, 1).astype(np.float32)
        if restore is not None:
            out *= restore.reshape(1, self.n, 1)
        return out.astype(np.float16)


# ---------------------------------------------------------------------------
# mul 族 decode (单发; 映射复用 test_u280_board 实证函数)
# ---------------------------------------------------------------------------

def _fill_sliced_contig(t, streams, data):
    """10-27 新交付的 sliced 填充: 每 SMC 一个连续 4KB 窗, 数据 = 全局
    张量片内段从窗头连续直摆 (有效 slice_len 元素, 尾部 padding 不写)。

    旧 test_u280_board.fill_sliced 是 10-19 旧交付的 sub 拆分模型
    (片内再均分到各 sub 的 chunk 头), 与新包"2 sub chunk=0x800 仅为
    装载粒度、数据窗内连续"不符 —— 照它写会在窗内留出 336 元素洞并
    整体错位 (10-27 selftest decode 4 case FAIL 的根因)。
    """
    lo, hi = t.span_of(streams)
    img = bytearray(hi - lo)
    slice_len = len(data) // NUM_SMC
    intlv = streams[0]["intlv"]
    assert all(s["intlv"] == intlv for s in streams), "sliced intlv 不一致"
    for k in range(NUM_SMC):
        off = k * intlv
        seg = data[k * slice_len:(k + 1) * slice_len].astype("<f2").tobytes()
        img[off:off + len(seg)] = seg
    return lo, bytes(img)


class MulDecodeRunner:
    """in0/in1 f16 任意形状 (元素数须与 OPS meta 一致) -> out0 spec 形."""

    def __init__(self, u, case_dir, case_name: str, out_shape: tuple):
        import test_u280_board as t
        self.t = t
        meta = t.OPS[case_name]
        if meta.get("rounds"):
            raise ValueError(f"{case_name} 是轮式 prefill 交付, "
                             f"本 runner 只接 decode 单发")
        self.u = u
        self.meta = meta
        self.out_shape = out_shape
        self.handle = u.store_case(case_dir)
        ent = u._case_entry(self.handle)
        streams = t.derive_streams(ent)
        self.groups = t.group_inputs(streams["ldb"])
        if len(self.groups) != 2:
            raise ValueError(f"{case_name} 输入分组 != 2: "
                             f"{[len(g) for g in self.groups]}")
        self.stb = streams["stb"]

    def run(self, in0: np.ndarray, in1: np.ndarray) -> np.ndarray:
        t, meta = self.t, self.meta
        a = np.ascontiguousarray(in0, dtype="<f2").reshape(-1)
        b = np.ascontiguousarray(in1, dtype="<f2").reshape(-1)
        if len(a) != meta["n_in0"] or len(b) != meta["n_in1"]:
            raise ValueError(
                "元素数不符: in0 %d/%d, in1 %d/%d" % (
                    len(a), meta["n_in0"], len(b), meta["n_in1"]))
        lo, img = _fill_sliced_contig(t, self.groups[0], a)
        self.u.h2c(img, lo, len(img))
        if meta["bcast"] is None:
            lo, img = _fill_sliced_contig(t, self.groups[1], b)
        else:
            lo, img = t.fill_bcast(self.groups[1], b)
        self.u.h2c(img, lo, len(img))
        # TB launch (首 stage cfg_rst 清场 + DONE 预清; 单 tile 边界不触发)
        _launch_case_tb(self.u, self.handle)
        if len(self.stb) == 2 * len(self.groups[0]):
            # 旧交付: 每 load sub 2 store sub, [512,176] 拆分 (10-19 定案)
            out, _, _ = t.read_store_region(self.u, self.stb,
                                            meta["n_out"], self.groups)
        else:
            # 10-27 新交付: store 16 sub/tile 连续 0x100 铺满 4KB 窗,
            # 落盘直 slice (同 MulPrefillRunner 读回)
            lo, span = _span(self.stb)
            img = self.u.c2h(lo, span)
            slice_n = meta["n_out"] // NUM_SMC
            out = np.zeros(meta["n_out"], dtype="<f2")
            seen = set()
            for s in self.stb:
                r = (s["base"] - lo) // 0x1000
                for k in range(NUM_SMC):
                    if (r, k) in seen:
                        continue
                    seen.add((r, k))
                    off = s["base"] + k * s["intlv"] - lo
                    # 4KB 窗内: 前 slice_n 元素有效, 其余为 padding
                    take = min(_TILE, slice_n - r * _TILE)
                    if take <= 0:
                        continue
                    out[k * slice_n + r * _TILE:
                        k * slice_n + r * _TILE + take] = \
                        np.frombuffer(img[off:off + take * 2], dtype="<f2")
        return out.reshape(self.out_shape).astype(np.float16)


# ---------------------------------------------------------------------------
# mul 族 prefill (10-27 修复交付; TB 等价 launch; 常规/dual6 双变体)
# ---------------------------------------------------------------------------

# (spec case 名: IQ 基目录, tile 粒度(元素), in1 bcast, n_in0, n_in1,
#  n_out, 无 dual6 变体标记)
_MUL_PREFILL_SPECS = {
    "mul_gate": ("prefill_mul_gate", 2048, None,
                 256 * 11008, 256 * 11008, 256 * 11008, False),
    "rmsnorm_mul_rsqrt": ("prefill_rmsnorm_mul_rsqrt", 2048,
                          "row_scalar", 256 * 4096, 256, 256 * 4096,
                          False),
    "rmsnorm_mul_weight": ("prefill_rmsnorm_mul_weight", 2048,
                           "row_tile", 256 * 4096, 4096, 256 * 4096,
                           False),
    "rope_mul_cos": ("prefill_rope_mul_cos", 2048, "row_vec",
                     256 * 4096, 256 * 128, 256 * 4096, True),
}
_TILE = 2048  # 每 SMC 每 tile 窗 (2K f16 = 4KB)


class MulPrefillRunner:
    """mul 族 prefill: (1,256,...) 常规/dual6 -> 同形输出.

    HBM 布局 (10-27 包流 dump 推导):
      全流 LDB g0/g1 为交错对 (sub0+sub1 bases 间隔 0x800), STB 为
      16 个子流/parallel-tile 连续 0x100 段铺满 4KB tile 窗; 所有
      "窗" 均为 base = tile*0x1000 + k*intlv 下的连续 4KB。
      in0/in1 sliced: RI=2048 —— 窗 r 存全局张量片内 [r*2048, +2048)
        直 slice (k 为 SMC 号, 片内偏移), dual6 双 tile 也同 (tile
        窗连续, dual6 唯一差别是 IQ stage 数减半)。
      in1 bcast (地址窗粒度恒 2048 元素; dual6 包无 bcast 变体):
        row_scalar (rsqrt 标量): 窗 r 起始 = 行 32k+r//2 标量 16 份
          (0x20B), smc 槽 0x800B 内仅头 0x20 有效, 其余 pad;
        row_tile (weight): intlv=0 全 SMC 共享, 内存 = 权重
          [r*2048, +2048) 连续直摆 (跨 r 铺, 跨 SMC 不拷);
        row_vec (rope cos): 窗 r 头 0x100B = 行 32k+r//2 的 128 f16
          整窗 (旧/10-27 新交付已确认 + 板上 100% 位精确 PASS)。
      store: 窗 r <- 全局输出片内 [r*2048, +2048) 直 slice 回填。
      bcast 次要槽位 (rsqrt 0x20~0x800, cos 0x100~0x800) 交付不消费,
      我们写 0 (投毒 0xAB 已在 _prefill_launch_tb 的框架底板处覆盖)。
    """

    def __init__(self, u, case_dir, out_shape: tuple):
        import test_u280_board as t
        name = Path(case_dir).name
        base = name[:-len("_dual6")] if name.endswith("_dual6") else name
        case = base[len("prefill_"):]
        spec = _MUL_PREFILL_SPECS[case]
        self.t = t
        self.u = u
        self.case, self.bcast = case, spec[2]
        self.n_in0, self.n_in1, self.n_out = spec[3], spec[4], spec[5]
        self.out_shape = out_shape
        self.handle = u.store_case(case_dir)
        ent = u._case_entry(self.handle)
        streams = t.derive_streams(ent)
        # 按跨流 identity 槽不分 sub (base >> 28), 两轮内 sub 同片内窗
        self.g0 = [r for r in streams["ldb"] if (r["base"] >> 28) == 0x0]
        self.g1 = [r for r in streams["ldb"] if (r["base"] >> 28) == 0x1]
        self.stb = streams["stb"]

    # ---- 序列化 ----

    def _write_ld_group(self, grp, data, rows_scalar=False, rows_vec=False,
                        weight=False):
        lo, img = self.t._img_for(grp)
        if weight:
            # intlv=0 共享: 连续 4KB 窗按 r 堆叠 = 权重全长直摆;
            # dual6 包 4 窗/16KB > 权重 8KB —— 整镜像循环铺设
            raw = data.astype("<f2").tobytes()
            img[:] = (raw * (len(img) // len(raw) + 1))[:len(img)]
            self.u.h2c(img, lo, len(img))
            return
        smc_n = len(data) // NUM_SMC
        # tile 下标 r 一律按各流自己的窗口步长从地址推导 (sliced 0x1000 /
        # 标量 0x20 / cos 0x100), 不能统一按 0x1000 —— 10-27 实证:
        # 统一按 0x1000 会把标量/cos 全部窗错写成 row 32k, sliced 被第
        # 二条 sub (base+0x800) 的整窗写覆盖成 [0:1024]x2。
        seen = set()
        for s in grp:
            for k in range(NUM_SMC):
                off = s["base"] + k * s["intlv"] - lo
                if rows_scalar:
                    r = (s["base"] - lo) // 0x20
                    img[off:off + 0x20] = np.full(
                        16, data[32 * k + r // 2], dtype="<f2").tobytes()
                elif rows_vec:
                    r = (s["base"] - lo) // 0x100
                    src = (32 * k + r // 2) * 128
                    img[off:off + 0x100] = data[src:src + 128].astype(
                        "<f2").tobytes()
                else:
                    r = (s["base"] - lo) // 0x1000
                    if (r, k) in seen:
                        continue  # 同窗第二条 sub (base+0x800), 已整窗写过
                    seen.add((r, k))
                    src = k * smc_n + r * _TILE
                    img[off:off + 0x1000] = data[src:src + _TILE].astype(
                        "<f2").tobytes()
        self.u.h2c(img, lo, len(img))

    # ---- run ----

    def run(self, in0: np.ndarray, in1: np.ndarray) -> np.ndarray:
        a = np.ascontiguousarray(in0, dtype="<f2").reshape(-1)
        b = np.ascontiguousarray(in1, dtype="<f2").reshape(-1)
        if len(a) != self.n_in0 or len(b) != self.n_in1:
            raise ValueError(
                "%s 元素数不符: in0 %d/%d, in1 %d/%d" % (
                    self.case, len(a), self.n_in0, len(b), self.n_in1))
        # in1 投毒 (0xAB) 压底层 bcast pad 槽 —— sliced/w=None 时同 in0
        stb_lo, stb_span = _span(self.stb)
        self.u.h2c(b"\xAB" * stb_span, stb_lo, stb_span)
        self._write_ld_group(self.g0, a)
        if self.bcast is None:
            self._write_ld_group(self.g1, b)
        elif self.bcast == "row_tile":
            self._write_ld_group(self.g1, b, weight=True)
        elif self.bcast == "row_scalar":
            self._write_ld_group(self.g1, b, rows_scalar=True)
        else:
            self._write_ld_group(self.g1, b, rows_vec=True)
        _launch_case_tb(self.u, self.handle)
        # 读回: out[k, r] = 区 [r*0x1000 + k*intlv, +4KB) -> [k*smc_n
        # + r*2048, +2048); 连续 4KB 窗内 16 个 0x100 sub 已铺满, 无洞
        img = self.u.c2h(stb_lo, stb_span)
        out = np.zeros(self.n_out, dtype="<f2")
        smc_n = self.n_out // NUM_SMC
        seen = set()
        for s in self.stb:
            r = (s["base"] - stb_lo) // 0x1000
            for k in range(NUM_SMC):
                if (r, k) in seen:
                    continue
                seen.add((r, k))
                off = s["base"] + k * s["intlv"] - stb_lo
                dst = k * smc_n + r * _TILE
                out[dst:dst + _TILE] = np.frombuffer(
                    img[off:off + 0x1000], dtype="<f2")
        return out.reshape(self.out_shape).astype(np.float16)


def _span(streams):
    lo = min(s["base"] for s in streams)
    hi = max(s["base"] + (NUM_SMC - 1) * s["intlv"] + s["chunk"]
             for s in streams)
    return lo, (hi - lo + 4095) // 4096 * 4096


# ---------------------------------------------------------------------------
# lmhead (500 轮 tiled; 映射复用 test_u280_lmhead_board 实证函数)
# ---------------------------------------------------------------------------

# lmhead 真实配对 (2026-10-27 单热探针 + jabari golden 双向定案, 取代文件
# 头 H1/k-major 假设): 板计算
#   out_slot(k,lane) = sum_p xb[p] * wb[G(p)+(3-lane)%8, k*8+nu(p)]
#   G(p)  = 256*(p>>8) + 32*(p&7) + 16*((p>>4)&1) + 8*((p>>7)&1)
#   nu(p) = 7 - ((p>>3)&1) - 2*((p>>5)&1) - 4*((p>>6)&1)
# (p = x 字节逻辑位置 t*256+k_x*32+j, wb 行列 = w_round_image 逻辑索引)。
# 12 个 x 位双射落位: {0,1,2}->行5-7, {4}->行4, {7}->行3, {8-11}->行8-11,
# {3,5,6}->列0-2; lane 在 8 行组内旋转 +3。因此 x 恒等喂入即可, W 逻辑
# (4096,64) 按下表预置换 [_LMH_P_INV, _LMH_N_OUT] 后走原 w_round_image,
# read_out 自然序即为逻辑输出序。证据: _lmhead_probe2.py 256 点残差 0,
# _lmhead_analyze.py 用 jabari 原始字节预测 bank7 golden 4 轮
# max|err|~5e-5 (fp32 累加次序量级)。
_LMH_J = np.arange(4096)[:, None]
_LMH_N = np.arange(64)[None, :]
_LMH_INV_NU = 7 - (_LMH_N & 7)
_LMH_P_INV = ((_LMH_J & 0xF00) | ((_LMH_J >> 5) & 7)
              | (((_LMH_J >> 4) & 1) << 4) | (((_LMH_J >> 3) & 1) << 7)
              | ((_LMH_INV_NU & 1) << 3) | (((_LMH_INV_NU >> 1) & 1) << 5)
              | (((_LMH_INV_NU >> 2) & 1) << 6))
_LMH_N_OUT = (_LMH_N >> 3) * 8 + ((3 - (_LMH_J & 7)) % 8)


class LmheadRunner:
    """in0 (1,4096) f16, in1 (32000,4096) f16 -> out0 (1,32000) f32.

    W 静态: 首次 run (或 warmup 显式调用) 时按 500 轮 x 512KB 上传,
    之后每 token 只传 x (8KB)。W 逻辑行/列按 _LMH_P_INV/_LMH_N_OUT
    预置换 (10-27 探针定案的真实配对, 见模块注释)。输出为 f32, 与
    板上 addtree 求和次序不同于 numpy 串行, 容差口径见
    test_u280_lmhead_board.py。
    """

    ROUNDS = 500

    def __init__(self, u, case_dir):
        import test_u280_lmhead_board as tl
        self.tl = tl
        self.u = u
        self.handle = u.store_case(case_dir)
        self.w_ready = False
        # W 低区与 rsqrt LUT / mul 族 in1 / matmul B 同起 0x10000000,
        # 交错推理会被冲掉 (rsqrt LUT 逐次重填就是同病先例)。
        # register_all 按各 runner 实际写跨距算出被覆盖的 W 前轮数
        # 写入 reload_rounds; 本 runner 缓存这些轮的镜像, 每次 run 重传
        # (512KB/轮 ~1s, 比全量 256MB 重传便宜三个数量级)。
        self.reload_rounds = 0
        self._w_prefix: list[bytes] = []

    def upload_weights(self, lm_head: np.ndarray) -> None:
        """lm_head (VOCAB, HIDDEN) f16 -> 500 轮 HBM 镜像 (一次性)."""
        tl = self.tl
        wt = tl.ftz_f16(np.ascontiguousarray(lm_head, dtype="<f2").T)
        t0 = time.time()
        self._w_prefix = []
        for r in range(self.ROUNDS):
            # 逻辑 (4096,64) -> 真实配对预置换 (10-27 定案)
            w_r = wt[:, r * 64:(r + 1) * 64][_LMH_P_INV, _LMH_N_OUT]
            img = bytes(tl.w_round_image(w_r))
            if r < self.reload_rounds:
                self._w_prefix.append(img)
            self.u.h2c(img, tl.W_BASE + r * tl.W_ROUND_STRIDE,
                       tl.W_ROUND_STRIDE)
            if (r + 1) % 100 == 0:
                print(f"[lmhead] W 上传 {r + 1}/{self.ROUNDS} 轮 "
                      f"({time.time() - t0:.1f}s)")
        self.w_ready = True

    def run(self, x: np.ndarray, w: np.ndarray) -> np.ndarray:
        if not self.w_ready:
            self.upload_weights(w)
        tl = self.tl
        # 重传被交错算子冲掉的 W 前轮 (每次调用; 未注册污染时为空表)
        for r, img in enumerate(self._w_prefix):
            self.u.h2c(img, tl.W_BASE + r * tl.W_ROUND_STRIDE,
                       tl.W_ROUND_STRIDE)
        xv = tl.ftz_f16(np.ascontiguousarray(x, dtype="<f2").reshape(-1))
        if len(xv) != tl.K_DIM:
            raise ValueError(f"lmhead x {len(xv)} != {tl.K_DIM}")
        lo, img = tl.x_image(xv)
        self.u.h2c(img, lo, len(img))
        # 首 stage cfg_rst 清场 + DONE 预清; 轮结构非 3 整除, 不做 tile 边界
        _launch_case_tb(self.u, self.handle, tile_rst=False)
        out, _ = tl.read_out(self.u, self.ROUNDS)
        return out.reshape(1, tl.N_DIM).astype(np.float32)


# ---------------------------------------------------------------------------
# matmul_nt_w4f16 decode (张文扬 10-27 交付, 板上 3 case x 3 seeds 位精确
# PASS; 布局/golden 定案见 _matmul_w4_verify.py 文件头)
# ---------------------------------------------------------------------------

# spec case 名: (IQ 基目录, K, nkb, S per-SMC 跨步)
_MM_DECODE_CASES = {
    "matmul_w4_qkvo":    ("decode_matmul_nt_w4f16_qkvo",    4096, 16, 0x2000),
    "matmul_w4_gate_up": ("decode_matmul_nt_w4f16_gate_up", 4096, 16, 0x2000),
    "matmul_w4_down":    ("decode_matmul_nt_w4f16_down",   11008, 43, 0x8000),
}
_MM_OUT_BASE = 0x70000000


def _unpack_u4(packed: np.ndarray) -> np.ndarray:
    """(N, W) uint8 打包 u4 -> (N, 2W) 逻辑 uint8 (偶数下标低 nibble)."""
    out = np.empty(packed.shape[:-1] + (packed.shape[-1] * 2,), np.uint8)
    out[..., 0::2] = packed & 0x0F
    out[..., 1::2] = packed >> 4
    return out


class MatmulW4Runner:
    """decode M=1 w4 GEMV: x (1,1,K) f16 + NT 三件套 -> out (1,1,N) f16.

    单次发射只算 32 列 (N-tile, col_base = tile*32); 全 N = N/32 次发射
    循环 (qkvo/o 128, gate/up 各 344, down 128 —— 每 token 每 layer
    1328 发)。布局 (8 SMC x 4 列, K 按 256/kb 切块, 跨 SMC 硬件累加):
      A     @0x0        (s,kb) 64B, 字 w∈{2s,2s+1,2s+16,2s+17},
                        lane row ↔ k = kb*256+32*row+W(s,w)
      B/W4  @0x10000000 (s,kb) 512B, 字 w: seg=w//4 列 4s+w%4,
                        k = kb*256+32*seg+8*(kk//8)+(7-kk%8), 字节 LO 先
      scale @0x20000000 (s,kb) 256B, 组 gi@+0x40: f16 @字节 (3-n)*4
                        ↔ scale[g=2kb+gi, 4s+n]
      zp    @0x30000000 同构; 组 gi@+0x40: 列 w 的 16B 全 = zp[g, 4s+w]
      st0   @0x40000000 bank7 累加器初值, 填 0 (每次 run 重填 —— 与
                        rsqrt LUT 同理, 防交错 case 的 store 冲掉)
      out   @0x70000000 SMC s 32B chunk, f16 槽 16s+2i = 列 4s+(3-i)
    A 镜像只依赖 x —— 每次 run 写一次; B/scale/zp 每 tile 换 32 列切片。
    权重解包 (N,K) u4/(N,G) 每次 run 重做 (numpy 向量化 ~10ms, 比
    发射便宜三个数量级, 不值得按 id 备忘几十 GB)。
    """

    def __init__(self, u, case_dir, K: int, nkb: int, S: int, N: int):
        self.u, self.K, self.nkb, self.S, self.N = u, K, nkb, S, N
        self.handle = u.store_case(case_dir)
        # 预计算全向量化索引 (flat 序 = (s, kb, ...) s 最慢; Z 强制广播到全形)
        S8 = np.arange(NUM_SMC)[:, None, None, None]
        KB = np.arange(nkb)[None, :, None, None]
        # ---- A: flat (s,kb,w,row) -> k; W(s,w) = 2s + w%2 + 16*(w//2) ----
        W4 = np.arange(4)[None, None, :, None]
        R8 = np.arange(8)[None, None, None, :]
        za = np.zeros((NUM_SMC, nkb, 4, 8), np.int64)
        Wv = 2 * S8 + (W4 % 2) + 16 * (W4 // 2)
        self._ia = (za + KB * 256 + 32 * R8 + Wv).reshape(-1)
        # ---- B: flat (s,kb,w,kk) -> (k, col_local) ----
        W32 = np.arange(32)[None, None, :, None]
        K32 = np.arange(32)[None, None, None, :]
        zb = np.zeros((NUM_SMC, nkb, 32, 32), np.int64)
        self._ib_k = (zb + KB * 256 + 32 * (W32 // 4)
                      + 8 * (K32 // 8) + (7 - K32 % 8)).reshape(-1)
        self._ib_c = (zb + 4 * S8 + W32 % 4).reshape(-1)
        # ---- scale/zp: flat (s,kb,gi,n) -> (g, col_local, 字节偏移) ----
        GI = np.arange(2)[None, None, :, None]
        N4 = np.arange(4)[None, None, None, :]
        zc = np.zeros((NUM_SMC, nkb, 2, 4), np.int64)
        self._ig = (zc + KB * 2 + GI).reshape(-1)
        self._ic = (zc + 4 * S8 + N4).reshape(-1)
        self._ioff_sc = (zc + S8 * S + KB * 0x100 + GI * 0x40
                         + (3 - N4) * 4).reshape(-1)
        off_zp = zc + S8 * S + KB * 0x100 + GI * 0x40 + N4 * 16
        self._ioff_zp = (off_zp[..., None] + np.arange(16)).reshape(-1)
        # ---- out: 逻辑列 d=4s+(3-i) <- flat 槽 16s+2i ----
        d = np.arange(32)
        self._ios = 16 * (d // 4) + 2 * (3 - d % 4)
        self._img_a = np.zeros(NUM_SMC * S, np.uint8)
        self._img_b = np.zeros(NUM_SMC * S, np.uint8)
        self._img_sc = np.zeros(NUM_SMC * S, np.uint8)
        self._img_zp = np.zeros(NUM_SMC * S, np.uint8)
        self._zeros = bytes(NUM_SMC * S)

    def _pack_a(self, x16: np.ndarray) -> bytes:
        """A 镜像 (只依赖 x): (s,kb) 64B 连续块排在 s*S 起."""
        a_vals = x16[self._ia].view(np.uint8).reshape(NUM_SMC, -1)
        self._img_a[:] = 0
        self._img_a.reshape(NUM_SMC, self.S)[:, :self.nkb * 64] = a_vals
        return self._img_a.tobytes()

    def _pack_tile(self, bq: np.ndarray, zp: np.ndarray, sc16: np.ndarray,
                   c0: int) -> tuple[bytes, bytes, bytes]:
        """tile (c0 起 32 列) 的 B/scale/zp 镜像; bq/zp/sc16 为全 N 逻辑阵
        ((N,K) u4 / (N,G) u4 / (N,G) f16)."""
        # B 镜像 (nibble 两两打包, 字节 LO 先)
        nib = bq[c0 + self._ib_c, self._ib_k]
        packed = (nib[0::2] | (nib[1::2] << 4)).reshape(NUM_SMC, -1)
        self._img_b[:] = 0
        self._img_b.reshape(NUM_SMC, self.S)[:, :self.nkb * 512] = packed
        # scale 镜像 (f16 LE 散字节 @ (3-n)*4)
        vals = sc16[c0 + self._ic, self._ig].view(np.uint8).reshape(-1, 2)
        self._img_sc[:] = 0
        self._img_sc[self._ioff_sc] = vals[:, 0]
        self._img_sc[self._ioff_sc + 1] = vals[:, 1]
        # zp 镜像 (列 w 16B 广播)
        self._img_zp[:] = 0
        self._img_zp[self._ioff_zp] = np.repeat(zp[c0 + self._ic, self._ig], 16)
        return (self._img_b.tobytes(), self._img_sc.tobytes(),
                self._img_zp.tobytes())

    def run(self, x: np.ndarray, qw: np.ndarray, sc: np.ndarray,
            qz: np.ndarray, max_tiles: int | None = None) -> np.ndarray:
        x16 = np.ascontiguousarray(x, dtype="<f2").reshape(-1)
        if len(x16) != self.K:
            raise ValueError(f"w4 matmul x {len(x16)} != K={self.K}")
        bq = _unpack_u4(qw)          # (N, K) 逻辑 u4
        zp = _unpack_u4(qz)          # (N, G)
        sc16 = np.ascontiguousarray(sc, dtype="<f2")   # (N, G)
        # bank7 累加器初值区填 0 (正常使用定案) —— 每次 run 重填
        self.u.h2c(self._zeros, 0x40000000, NUM_SMC * self.S)
        # A 镜像: 全调用一次
        self.u.h2c(self._pack_a(x16), 0x0, NUM_SMC * self.S)
        out = np.zeros(self.N, dtype="<f2")
        tiles = self.N // 32
        if max_tiles is not None:
            tiles = min(tiles, max_tiles)
        for t in range(tiles):
            img_b, img_sc, img_zp = self._pack_tile(bq, zp, sc16, t * 32)
            self.u.h2c(img_b, 0x10000000, NUM_SMC * self.S)
            self.u.h2c(img_sc, 0x20000000, NUM_SMC * self.S)
            self.u.h2c(img_zp, 0x30000000, NUM_SMC * self.S)
            # 发射 (35/89 stage 非 3 整除: 仅 case 首 cfg_rst + DONE 预清)
            _launch_case_tb(self.u, self.handle, tile_rst=False)
            flat = np.frombuffer(
                self.u.c2h(_MM_OUT_BASE, 0x100), dtype="<f2")
            out[t * 32:t * 32 + 32] = flat[self._ios]
        return out.reshape(1, 1, self.N).astype(np.float16)


# ---------------------------------------------------------------------------
# rotate_half (李思颖 20261026 替换交付 + 10-27 21:24 IQ 指令流修复;
# 2026-10-28 _rotate_neg_probe / _newops_add_rotate_test 双相位全位精确
# PASS 后接线。纯置换+XOR 无值域依赖, 与 rsqrt 次正规坑不同类)
# ---------------------------------------------------------------------------

_ROT_INTLV = 0x2000000        # A/mask/out 三区的 SMC stride
_ROT_MASK_BASE = 0x10000000   # XOR 符号位掩码区 (在 lmhead W 区内, 逐 run 重写)
_ROT_OUT_BASE = 0x70000000
# 4096B 掩码: 16 行 x 128 u16, 每行前 64 个 0x8000 —— 位掩码非浮点系数,
# 硬件每轮重读同一张表 (_newops_add_rotate_test.run_rotate 实证布局)
_ROT_MASK_BYTES = np.tile(np.concatenate(
    [np.full(64, 0x8000, "<u2"), np.zeros(64, "<u2")]), 16).tobytes()


class RotateRunner:
    """in0 (1, seq, 32, 128) f16 -> out0 同形; out = cat(-x[...,64:], x[...,:64]).

    逻辑 n 元素按 SMC 连续均分 (smc_n = n//8), 每 SMC 物理 rounds*2048
    元素, 逻辑外补零: decode n=4096 (2 轮, smc_n=512) /
    prefill n=256*4096 (64 轮, smc_n=131072)。A 与掩码每次 run 重写
    (掩码区与 lmhead W / rsqrt LUT 同在 0x10000000 起, 交错会被冲)。
    """

    def __init__(self, u, case_dir, rounds: int, n: int):
        self.u = u
        self.rounds = rounds
        self.n = n
        self.smc_n = n // NUM_SMC
        self.handle = u.store_case(case_dir)

    def run(self, x: np.ndarray) -> np.ndarray:
        vals = np.ascontiguousarray(x, dtype="<f2").reshape(-1)
        if len(vals) != self.n:
            raise ValueError(
                f"rotate_half 输入 {len(vals)} != 登记 {self.n}")
        span = self.rounds * 2048 * 2   # 每 SMC 物理字节数
        x_phys = np.zeros((NUM_SMC, self.rounds * 2048), dtype="<f2")
        x_phys[:, :self.smc_n] = vals.reshape(NUM_SMC, self.smc_n)
        for k in range(NUM_SMC):
            self.u.h2c(x_phys[k].tobytes(), k * _ROT_INTLV, span)
            self.u.h2c(_ROT_MASK_BYTES, _ROT_MASK_BASE + k * _ROT_INTLV,
                       len(_ROT_MASK_BYTES))
        _launch_case_tb(self.u, self.handle)
        got = np.empty((NUM_SMC, self.rounds * 2048), dtype="<f2")
        for k in range(NUM_SMC):
            got[k] = np.frombuffer(
                self.u.c2h(_ROT_OUT_BASE + k * _ROT_INTLV, span),
                dtype="<f2")
        return got[:, :self.smc_n].reshape(x.shape).astype(np.float16)


# ---------------------------------------------------------------------------
# reduce_mean_square (rm_fixed2 2026-10-29 交付, meta_v3 新格式; 自带
# runner 板上 prefill/decode 双 PASS。落盘布局 host 取证定案: 与交付
# golden 逐 lane 对拍 max 8.1e-7 / rel 1.2e-5, 见 _rm_*.py 取证链)
# ---------------------------------------------------------------------------

_RM_STRIDE = 0x10000000    # 每 SMC 窗口跨步
_RM_EPS = 1.0e-5
_RM_C_EPS16 = np.full(4, _RM_EPS, "<f4").tobytes()                 # c_eps 1 字
_RM_C_INV32 = np.full(4, 1.0 / 4096.0, "<f4").tobytes() + bytes(16)  # c_inv 2 字


class ReduceMeanRunner:
    """reduce_mean_square: x (1,R,4096) f16 -> ms (1,R,1) f16 = mean(x²)+eps.

    每 SMC 窗口 (stride 0x10000000) 布局 (host 取证定案):
      c_eps @0x0     1 字 = f32 eps ×4
      c_inv @0x1000  2 字 = f32 (1/4096)×4, 全 0
      c_out @0x2000  echo 区 (compute 不消费, store 回显到输出头部), 填 0
      val0  @0x3000  行前半 2048 元素
      val1  @0x23000 (prefill) / 0x4000 (decode) 行后半 2048 元素
      out   @0x43000 (prefill 96 字: [0..32) echo, 字 [32+2r],[32+2r+1] =
                     行 r 的 8 条 f32 acc lane) / @0x5000 (decode 4 字,
                     字 [2],[3] = lane)
    行 g (窗内) = val0[g*2048:(g+1)*2048] ++ val1[同段] (2KB 交错);
    输出行序恒等。prefill 全局行 32s+r → SMC s; decode 8 SMC 冗余同一行,
    smc0 读回。ms = Σ 8 lane − 7·eps (每 lane 自带 +eps 实证)。常量/echo
    每次 run 重写 (与其他 runner 的输入重写纪律一致)。
    """

    def __init__(self, u, case_dir, n_rows: int):
        self.u, self.n_rows = u, n_rows
        if n_rows == 1:
            self.val1_off, self.store_off = 0x4000, 0x5000
            self.store_bytes, self.echo_words = 64, 2
        else:
            self.val1_off, self.store_off = 0x23000, 0x43000
            self.store_bytes, self.echo_words = 1536, 32
        self.handle = u.store_case(case_dir)

    def run(self, x: np.ndarray) -> np.ndarray:
        xf = np.ascontiguousarray(x, dtype="<f2").reshape(-1)
        if len(xf) != self.n_rows * 4096:
            raise ValueError(
                f"reduce_mean 输入 {len(xf)} != {self.n_rows}*4096")
        xf = xf.reshape(self.n_rows, 4096)
        rows = (np.broadcast_to(xf, (NUM_SMC, 1, 4096)) if self.n_rows == 1
                else xf.reshape(NUM_SMC, 32, 4096))
        for s in range(NUM_SMC):
            base = s * _RM_STRIDE
            v = np.ascontiguousarray(rows[s])
            self.u.h2c(_RM_C_EPS16, base, len(_RM_C_EPS16))
            self.u.h2c(_RM_C_INV32, base + 0x1000, len(_RM_C_INV32))
            self.u.h2c(bytes(self.echo_words * 16), base + 0x2000,
                       self.echo_words * 16)
            self.u.h2c(v[:, :2048].tobytes(), base + 0x3000,
                       v.shape[0] * 4096)
            self.u.h2c(v[:, 2048:].tobytes(), base + self.val1_off,
                       v.shape[0] * 4096)
        # 482/17 stage 非 3 整除: 仅 case 首 cfg_rst + DONE 预清
        _launch_case_tb(self.u, self.handle, tile_rst=False)
        if self.n_rows == 1:
            lanes = np.frombuffer(self.u.c2h(self.store_off, 64), "<f4")[8:16]
            ms = float(lanes.sum() - 7 * _RM_EPS)     # smc0 (store mask 0x1)
            return np.array([[[ms]]], dtype=np.float16)
        raw = b"".join(
            self.u.c2h(s * _RM_STRIDE + self.store_off, self.store_bytes)
            for s in range(NUM_SMC))
        acc = np.frombuffer(raw, "<f4").reshape(NUM_SMC, 96, 4)
        ms = acc[:, 32:, :].reshape(NUM_SMC, 32, 8).sum(axis=2) - 7 * _RM_EPS
        return ms.reshape(1, self.n_rows, 1).astype(np.float16)


# ---------------------------------------------------------------------------
# matmul_nt_w4f16 prefill M=256 (张文扬 10-28 交付, git c22e8f04)。原交付
# store (1×STB128 + 64×AXI brst1) 真机死锁 —— 10-29 定案 (单 STB 喂多
# AXI 吃满 line_cnt 必挂), _m16 = 同 case 链接 + mul 形态 store 修复
# (8×[STB16 + AXI brst15]), 3 case × 3 seeds 板上 PASS (seed0 位精确),
# 详见 docs/matmul_prefill_store_deadlock_20261029.md。
# 全 N = N/32 次发射 (qkvo/gate_up/down = 128/344/128 发 × 每发
# 4166/4166/11186 stage ≈ 分钟级) —— 常规 e2e 不现实, llama2_hybrid
# 默认 host; 本 runner 供正确性对拍 (LLAMA2_MM_PREFILL=board 或
# selftest max_tiles 抽检)。
# ---------------------------------------------------------------------------

# spec case 名: (IQ 基目录, K, nkb, S per-SMC 跨步) —— 均指 _m16 修复变体
_MM_PREFILL_CASES = {
    "matmul_w4_qkvo":    ("prefill_matmul_nt_w4f16_qkvo_m16",    4096, 128, 0x40000),
    "matmul_w4_gate_up": ("prefill_matmul_nt_w4f16_gate_up_m16", 4096, 128, 0x40000),
    "matmul_w4_down":    ("prefill_matmul_nt_w4f16_down_m16",   11008, 344, 0x100000),
}
_MM_PF_OUT_BYTES = 0x4000    # 64 块 × 256B (D5/m16 同尺寸, 几何不同)
_MM_PF_ZERO_BYTES = 0x1000   # st0 bank7 累加器清零零区 (stage_0 LDB 256 字)


class PrefillMatmulW4Runner:
    """prefill M=256 w4 GEMM: x (1,256,K) f16 + NT 三件套 -> (1,256,N) f16.

    单次发射算 32 列 (N-tile); 布局 (_prefill_mm_verify.py 定案, pack/
    unpack 已位精确验证, 此处向量化复刻):
      A     @0x0        (s,kb) 0x800B: 字 4g+j lane r = 行 8g+r,
                        k = kb*32 + (2s,2s+1,2s+16,2s+17)[j]
      B/W4  @0x10000000 (s,kb) 0x40B: 列 4s+w 的 32 nibble,
                        nibble i ↔ k = kb*32+8*(i//8)+(7-i%8), 字节 LO 先
      scale @0x20000000 (s,kb) 0x40B: f16 LE @字节 (3-n)*4
                        ↔ scale[kb//4, 4s+n]
      zp    @0x30000000 (s,kb) 0x40B: 字 w 全 16B = zp[kb//4, 4s+w]
      st0   @0x40000000 每 SMC 0x1000B 清零零区 (每次 run 重填)
      out   @0x70000000 0x4000B, m16 几何: 字节 (w>>4)*0x800 + s*0x100
                        + (w&15)*0x10 = bank6 字 w (行对 2w/2w+1) SMC s;
                        字内 u16[2n]=f16(行偶, 列 4s+3-n), u16[2n+1]=行奇
    A 只依赖 x —— 每 run 打包一次; B/scale/zp 每 tile 换 32 列切片。
    权重解包/转置每 run 重做 (numpy 向量化, 与 decode 同理不备忘)。
    """

    def __init__(self, u, case_dir, K: int, nkb: int, S: int, N: int):
        self.u, self.K, self.nkb, self.S, self.N = u, K, nkb, S, N
        self.handle = u.store_case(case_dir)
        # ---- A: flat (s,kb,w,r) -> (row, k); w=4g+j ----
        S8 = np.arange(NUM_SMC)[:, None, None, None]
        KB = np.arange(nkb)[None, :, None, None]
        W128 = np.arange(128)[None, None, :, None]
        R8 = np.arange(8)[None, None, None, :]
        kk4 = np.stack([2 * np.arange(NUM_SMC) + j
                        for j in (0, 1, 16, 17)], axis=1)      # (8,4)
        za = np.zeros((NUM_SMC, nkb, 128, 8), np.int64)
        self._ia_r = (za + 8 * (W128 // 4) + R8).reshape(-1)
        self._ia_k = (za + KB * 32 + kk4[S8, W128 % 4]).reshape(-1)
        # ---- B: flat (s,kb,w,j) 字节 <- nibble 对 (i0=2j, i1=2j+1) ----
        W4 = np.arange(4)[None, None, :, None]
        J16 = np.arange(16)[None, None, None, :]
        koff = 8 * (np.arange(32) // 8) + (7 - np.arange(32) % 8)  # (32,)
        zb = np.zeros((NUM_SMC, nkb, 4, 16), np.int64)
        self._ib_c = (zb + 4 * S8 + W4).reshape(-1)
        self._ib_k0 = (zb + KB * 32 + koff[2 * J16]).reshape(-1)
        self._ib_k1 = (zb + KB * 32 + koff[2 * J16 + 1]).reshape(-1)
        self._ioff_b = (zb + S8 * S + KB * 0x40 + W4 * 16 + J16).reshape(-1)
        # ---- scale: flat (s,kb,n) -> (g, col, 字节 (3-n)*4) ----
        S8b = np.arange(NUM_SMC)[:, None, None]
        KBb = np.arange(nkb)[None, :, None]
        N4b = np.arange(4)[None, None, :]
        zs = np.zeros((NUM_SMC, nkb, 4), np.int64)
        self._isc_g = (zs + KBb // 4).reshape(-1)
        self._isc_c = (zs + 4 * S8b + N4b).reshape(-1)
        self._ioff_sc = (zs + S8b * S + KBb * 0x40
                         + (3 - N4b) * 4).reshape(-1)
        # ---- zp: flat (s,kb,w,j) 全 16B 广播 ----
        W4z = np.arange(4)[None, None, :, None]
        J16z = np.arange(16)[None, None, None, :]
        zz = np.zeros((NUM_SMC, nkb, 4, 16), np.int64)
        self._izp_g = (zz + KB // 4).reshape(-1)
        self._izp_c = (zz + 4 * S8 + W4z).reshape(-1)
        self._ioff_zp = (zz + S8 * S + KB * 0x40 + W4z * 16
                         + J16z).reshape(-1)
        # ---- out m16: u16 索引 (w, s, lane) -> flat ----
        w128 = np.arange(128)[:, None, None]
        s8o = np.arange(NUM_SMC)[None, :, None]
        l8 = np.arange(8)[None, None, :]
        self._ioff_out = (((w128 >> 4) * 0x800 + s8o * 0x100
                           + (w128 & 15) * 0x10) // 2 + l8).reshape(-1)
        self._img_a = np.zeros(NUM_SMC * S, np.uint8)
        self._img_b = np.zeros(NUM_SMC * S, np.uint8)
        self._img_sc = np.zeros(NUM_SMC * S, np.uint8)
        self._img_zp = np.zeros(NUM_SMC * S, np.uint8)

    def _pack_a(self, a16: np.ndarray) -> None:
        """A 镜像 (只依赖 x): 每 SMC nkb*0x800B 连续块排在 s*S 起."""
        vals = a16[self._ia_r, self._ia_k].view(np.uint8)
        self._img_a[:] = 0
        self._img_a.reshape(NUM_SMC, self.S)[:, :self.nkb * 0x800] = \
            vals.reshape(NUM_SMC, -1)

    def _pack_tile(self, bq: np.ndarray, zp: np.ndarray, sc16: np.ndarray,
                   c0: int) -> None:
        """tile (c0 起 32 列) 的 B/scale/zp 镜像; bq/zp/sc16 为转置后
        逻辑阵 ((K,N) u4 / (G,N) u4 / (G,N) f16)."""
        self._img_b[:] = 0
        self._img_b[self._ioff_b] = (bq[self._ib_k0, c0 + self._ib_c]
                                     | (bq[self._ib_k1, c0 + self._ib_c]
                                        << 4))
        vals = sc16[self._isc_g, c0 + self._isc_c]
        vals = vals.view(np.uint8).reshape(-1, 2)
        self._img_sc[:] = 0
        self._img_sc[self._ioff_sc] = vals[:, 0]
        self._img_sc[self._ioff_sc + 1] = vals[:, 1]
        self._img_zp[:] = 0
        self._img_zp[self._ioff_zp] = zp[self._izp_g, c0 + self._izp_c] & 0xF

    def _unpack_out(self, raw: bytes) -> np.ndarray:
        """m16 几何 -> (256, 32) f16 (向量化版 unpack_out_m16)."""
        u16 = np.frombuffer(raw, dtype="<u2")
        words = u16[self._ioff_out].reshape(128, NUM_SMC, 4, 2)  # [w,s,n,p]
        gotu = np.zeros((256, 32), dtype="<u2")
        w = np.arange(128)[:, None, None, None]
        rr = 2 * w + np.arange(2)[None, None, None, :]
        cc = (4 * np.arange(NUM_SMC)[None, :, None, None]
              + 3 - np.arange(4)[None, None, :, None])
        gotu[rr, cc] = words
        return gotu.view("<f2")

    def run(self, x: np.ndarray, qw: np.ndarray, sc: np.ndarray,
            qz: np.ndarray, max_tiles: int | None = None) -> np.ndarray:
        a16 = np.ascontiguousarray(x, dtype="<f2").reshape(-1)
        if len(a16) != 256 * self.K:
            raise ValueError(
                f"w4 matmul prefill x {len(a16)} != 256*{self.K}")
        a16 = a16.reshape(256, self.K)
        bq = _unpack_u4(qw).T                    # (K, N) 逻辑 u4
        zp = _unpack_u4(qz).T                    # (G, N)
        sc16 = np.ascontiguousarray(sc, dtype="<f2").T   # (G, N)
        self._pack_a(a16)
        for s in range(NUM_SMC):
            self.u.h2c(bytes(self._img_a.reshape(NUM_SMC, self.S)[s]),
                       s * self.S, self.S)
            # bank7 累加器清零零区 —— 每次 run 重填
            self.u.h2c(bytes(_MM_PF_ZERO_BYTES),
                       0x40000000 + s * self.S, _MM_PF_ZERO_BYTES)
        out = np.zeros((256, self.N), dtype="<f2")
        tiles = self.N // 32
        if max_tiles is not None:
            tiles = min(tiles, max_tiles)
        for t in range(tiles):
            c0 = t * 32
            self._pack_tile(bq, zp, sc16, c0)
            for s in range(NUM_SMC):
                self.u.h2c(bytes(self._img_b.reshape(NUM_SMC, self.S)[s]),
                           0x10000000 + s * self.S, self.S)
                self.u.h2c(bytes(self._img_sc.reshape(NUM_SMC, self.S)[s]),
                           0x20000000 + s * self.S, self.S)
                self.u.h2c(bytes(self._img_zp.reshape(NUM_SMC, self.S)[s]),
                           0x30000000 + s * self.S, self.S)
            _launch_case_tb(self.u, self.handle, tile_rst=False)
            raw = self.u.c2h(_MM_OUT_BASE, _MM_PF_OUT_BYTES)
            out[:, c0:c0 + 32] = self._unpack_out(raw)
        return out.reshape(1, 256, self.N).astype(np.float16)


# ---------------------------------------------------------------------------
# 注册
# ---------------------------------------------------------------------------

_MUL_DECODE_CASES = ("rmsnorm_mul_rsqrt", "rmsnorm_mul_weight",
                     "rope_mul_cos", "mul_gate")
_MUL_PREFILL_CASES = _MUL_DECODE_CASES  # 同 4 名, phase=prefill


def _streams_hi(streams) -> int:
    """流组写入上界: max(base + 7*intlv + chunk)."""
    return max(s["base"] + (NUM_SMC - 1) * s["intlv"] + s["chunk"]
               for s in streams)


def _w_clobber_hi(runner) -> int:
    """runner 每次 run 在 lmhead W 区 [0x10000000, 0x1FA00000) 的写入上界.

    (W 区与 rsqrt LUT / mul 族 in1 / matmul B 同起 0x10000000; 0 = 不污染)
    """
    if isinstance(runner, RsqrtRunner):
        # LUT 64B @0x10000000 + in0 k=1 窗 @0x10001000
        return _RS_IN0_BASE + _RS_MAGIC_STRIDE + _RS_WIN
    if isinstance(runner, MulDecodeRunner):
        hi = 0
        for g in runner.groups:
            if 0x10000000 <= min(s["base"] for s in g) < 0x1FA00000:
                hi = max(hi, _streams_hi(g))
        return hi
    if isinstance(runner, MulPrefillRunner):
        return _streams_hi(runner.g1) if runner.g1 else 0
    if isinstance(runner, MatmulW4Runner):
        return 0x10000000 + NUM_SMC * runner.S   # B 镜像区
    if isinstance(runner, PrefillMatmulW4Runner):
        return 0x10000000 + NUM_SMC * runner.S   # B 镜像区 (同 decode)
    if isinstance(runner, ReduceMeanRunner):
        # 仅 SMC1 窗口起 0x10000000 与 W 区相交: 常量+val+out 全在里面
        return _RM_STRIDE + runner.store_off + runner.store_bytes
    if isinstance(runner, RotateRunner):
        # 掩码 4KB/SMC @0x10000000+k*0x2000000 (A 低于 W 区, out 高于)
        return (_ROT_MASK_BASE + (NUM_SMC - 1) * _ROT_INTLV
                + len(_ROT_MASK_BYTES))
    return 0


def register_all(u, runners: dict, iq_root: Path | str = "IQ",
                 prefill_dual6: bool | None = None) -> None:
    """把全部已验证交付的 runner 注册进 llama2_hybrid.RUNNERS."""
    if prefill_dual6 is None:
        prefill_dual6 = os.environ.get("MUL_PREFILL_DUAL6", "") == "1"
    import iq_map            # 本地 import, 保持 python3 board_runners.py 免依赖
    iq_root = Path(iq_root)
    runners[("prefill", "rsqrt")] = RsqrtRunner(
        u, iq_root / "prefill_rsqrt", 256)
    runners[("decode", "rsqrt")] = RsqrtRunner(
        u, iq_root / "decode_rsqrt", 1)
    for case in _MUL_DECODE_CASES:
        spec = iq_map.by_case(case, "decode")
        runners[("decode", case)] = MulDecodeRunner(
            u, iq_root / f"decode_{case}", f"decode_{case}",
            spec.outputs[0].shape)
    for case in _MUL_PREFILL_CASES:
        spec = iq_map.by_case(case, "prefill")
        base = _MUL_PREFILL_SPECS[case][0]
        # rope_mul_cos 本批无 dual6 (README: BCAST/EMBED 暂不支持)
        use_dual6 = prefill_dual6 and not _MUL_PREFILL_SPECS[case][6]
        dir_name = base + ("_dual6" if use_dual6 else "")
        runners[("prefill", case)] = MulPrefillRunner(
            u, iq_root / dir_name, spec.outputs[0].shape)
    runners[("common", "lmhead_matmul_nt_f16")] = LmheadRunner(
        u, iq_root / "lmhead_matmul_nt_f16")
    # rotate_half 双 phase (10-27 21:24 IQ 修复, 10-28 位精确 PASS)
    runners[("decode", "rotate_half")] = RotateRunner(
        u, iq_root / "decode_rotate_half", 2, 4096)
    runners[("prefill", "rotate_half")] = RotateRunner(
        u, iq_root / "prefill_rotate_half", 64, 256 * 4096)
    # matmul_nt_w4f16 decode x3 (10-27 交付位精确 PASS; 2026-10-28 接线)
    for case, (d, K, nkb, S) in _MM_DECODE_CASES.items():
        spec = iq_map.by_case(case, "decode")
        runners[("decode", case)] = MatmulW4Runner(
            u, iq_root / d, K, nkb, S, spec.outputs[0].shape[-1])
    # reduce_mean_square 双 phase (rm_fixed2 10-29 交付; 布局 host 取证
    # 定案后接线; llama2_hybrid 默认 board, LLAMA2_RM=host 回退)
    runners[("prefill", "reduce_mean_square")] = ReduceMeanRunner(
        u, iq_root / "reduce_mean_lcs_fpga", 256)
    runners[("decode", "reduce_mean_square")] = ReduceMeanRunner(
        u, iq_root / "reduce_mean_lcs_fpga_decode", 1)
    # prefill w4 matmul x3 (_m16 store 修复变体; 全 N 发射分钟级/tile,
    # llama2_hybrid 默认仍 host, LLAMA2_MM_PREFILL=board 上板对拍)
    for case, (d, K, nkb, S) in _MM_PREFILL_CASES.items():
        spec = iq_map.by_case(case, "prefill")
        runners[("prefill", case)] = PrefillMatmulW4Runner(
            u, iq_root / d, K, nkb, S, spec.outputs[0].shape[-1])
    variant = ("dual6(gate 258, rmsnorm/weight 96 stage)"
               if prefill_dual6 else "常规(516/192/192 stage)")
    print(f"[board] 已注册 {len(runners)} 个板上 runner, "
          f"mul prefill = {variant}")
    # lmhead W 低区与 rsqrt LUT / mul in1 / matmul B 同在 0x10000000 起,
    # 交错推理会被冲掉 —— 按各 runner 实际写跨距算出需逐次重传的 W 前轮数
    lmh = runners[("common", "lmhead_matmul_nt_f16")]
    w_top = lmh.tl.W_BASE + LmheadRunner.ROUNDS * lmh.tl.W_ROUND_STRIDE
    hi = 0
    for r in runners.values():
        if r is not lmh:
            hi = max(hi, _w_clobber_hi(r))
    hi = min(hi, w_top)
    if hi > lmh.tl.W_BASE:
        lmh.reload_rounds = -(-(hi - lmh.tl.W_BASE) // lmh.tl.W_ROUND_STRIDE)
    print(f"[board] lmhead W 交错污染上界 {hi:#x}, 每次调用重传前 "
          f"{lmh.reload_rounds} 轮 ({lmh.reload_rounds * 512}KB)")
    # packer 快写 (matmul 每 launch 25072 beat 的 30us/beat -> ~8us)
    _install_fast_packer(u)


def warmup(runners: dict, lm_head: np.ndarray) -> None:
    """提前上传 lmhead W (256MB, 分钟级), 避免首 token 卡顿无提示."""
    r = runners.get(("common", "lmhead_matmul_nt_f16"))
    if r is not None and not r.w_ready:
        r.upload_weights(lm_head)


# ---------------------------------------------------------------------------
# 自测 (需板卡 + sudo): 各 runner 随机输入 vs numpy golden
# ---------------------------------------------------------------------------

def _selftest(prefill_dual6: bool) -> int:
    sys.path.insert(0, str(ROOT / "test"))
    import operators as op   # noqa: E402
    import iq_map            # noqa: E402
    from u280 import U280    # noqa: E402

    rng = np.random.default_rng(20261027)
    fails = 0

    def check(name, got, want, atol=0.01):
        nonlocal fails
        err = np.abs(got.astype(np.float32) - want.astype(np.float32))
        ok = bool((err <= atol).all())
        print("  [%s] %s max|err|=%.4g" % (
            "PASS" if ok else "FAIL", name, err.max()))
        fails += 0 if ok else 1

    with U280(backend="xdma", timeout_s=30.0) as u:
        u.reset_smi()
        runners: dict = {}
        register_all(u, runners, prefill_dual6=prefill_dual6)

        x1 = rng.uniform(0.5, 2.0, (1, 1, 1)).astype(np.float16)
        mg = np.zeros(128, dtype=np.uint8)  # 契约占位, runner 内部用实证表
        check("decode/rsqrt", runners[("decode", "rsqrt")].run(x1, mg),
              op.rsqrt(x1))
        x256 = rng.uniform(0.5, 2.0, (1, 256, 1)).astype(np.float16)
        check("prefill/rsqrt", runners[("prefill", "rsqrt")].run(x256, mg),
              op.rsqrt(x256))
        # 次正规回归 (2026-10-28 定案): 板上位技巧在输入 <2^-14 (f16
        # 次正规) 时失效 —— e2e 层0 真实 ms (EPS 底 1e-5 pad 行 /
        # 4.15e-5 逗号行) 正落此区, 曾致 prefill cos=-0.71。
        # _rsqrt_prescale 4^k 抬升修复后应回板固有精度 (rel<=2e-3)。
        # 绝对值跨度 129..316, 用相对误差判据。
        xsub = np.concatenate(
            [rng.uniform(1e-5, 6e-5, (1, 128, 1)),
             rng.uniform(0.5, 2.0, (1, 128, 1))], axis=1).astype(np.float16)
        got = runners[("prefill", "rsqrt")].run(xsub, mg).astype(np.float32)
        want = op.rsqrt(xsub).astype(np.float32)
        rel = np.abs(got - want) / want
        ok = bool((rel <= 2e-3).all())
        print("  [%s] prefill/rsqrt-subnormal max|rel|=%.4g" % (
            "PASS" if ok else "FAIL", rel.max()))
        fails += 0 if ok else 1
        x1sub = np.array([[[4.15e-5]]], dtype=np.float16)
        got = runners[("decode", "rsqrt")].run(x1sub, mg).astype(np.float32)
        want = op.rsqrt(x1sub).astype(np.float32)
        rel = float(np.abs(got - want) / want)
        ok = rel <= 2e-3
        print("  [%s] decode/rsqrt-subnormal rel=%.4g" % (
            "PASS" if ok else "FAIL", rel))
        fails += 0 if ok else 1

        # rotate_half 双 phase (10-27 21:24 IQ 指令流修复, 10-28
        # _newops_add_rotate_test 双相位全位精确 PASS): 纯置换+XOR
        # 符号位, 无值域依赖, 判逐位一致 (atol=0)
        def rot_golden(x):
            xr = x.astype(np.float32).reshape(-1, 128)
            return np.concatenate(
                [-xr[:, 64:], xr[:, :64]], axis=1
            ).astype(np.float16).reshape(x.shape)
        xr1 = rng.uniform(-2.0, 2.0, (1, 1, 32, 128)).astype(np.float16)
        check("decode/rotate_half",
              runners[("decode", "rotate_half")].run(xr1),
              rot_golden(xr1), atol=0.0)
        xr256 = rng.uniform(-2.0, 2.0, (1, 256, 32, 128)).astype(np.float16)
        check("prefill/rotate_half",
              runners[("prefill", "rotate_half")].run(xr256),
              rot_golden(xr256), atol=0.0)

        # reduce_mean_square 双 phase (rm_fixed2 10-29 交付; 布局定案:
        # 行 = val0/val1 2KB 交错, ms = Σ8 f32 lane − 7·eps)
        xm = rng.uniform(0.5, 2.0, (1, 256, 4096)).astype(np.float16)
        check("prefill/reduce_mean_square",
              runners[("prefill", "reduce_mean_square")].run(xm),
              op.reduce_mean_square(xm))
        xm1 = rng.uniform(0.5, 2.0, (1, 1, 4096)).astype(np.float16)
        check("decode/reduce_mean_square",
              runners[("decode", "reduce_mean_square")].run(xm1),
              op.reduce_mean_square(xm1))

        for case in _MUL_DECODE_CASES:
            spec = iq_map.by_case(case, "decode")
            ins = [rng.uniform(-2.0, 2.0, t.shape).astype(np.float16)
                   for t in spec.inputs]
            check("decode/" + case, runners[("decode", case)].run(*ins),
                  op.mul(*ins))

        for case in _MUL_PREFILL_CASES:
            spec = iq_map.by_case(case, "prefill")
            ins = [rng.uniform(-2.0, 2.0, t.shape).astype(np.float16)
                   for t in spec.inputs]
            r = runners[("prefill", case)]
            check("prefill/" + case, r.run(*ins), op.mul(*ins))

        x = rng.standard_normal((1, 4096)).astype(np.float16) * 0.02
        w = (rng.standard_normal((32000, 4096)) * 0.02).astype(np.float16)
        got = runners[("common", "lmhead_matmul_nt_f16")].run(x, w)
        want = x.astype(np.float32) @ w.astype(np.float32).T
        check("common/lmhead_matmul_nt_f16", got, want, atol=0.05)
        lm_x, lm_w, lm_got = x, w, got   # 交错回归基准 (matmul 循环会改 x)

        # matmul_w4 decode: 2 tile 抽检 (tile 间独立, 全 N 与逐位对拍已在
        # _matmul_w4_verify.py 3 case x 3 seeds 位精确 PASS)
        for case, (_, K, _, _) in _MM_DECODE_CASES.items():
            spec = iq_map.by_case(case, "decode")
            N, G = spec.outputs[0].shape[-1], K // iq_map.GROUP
            x = (rng.standard_normal((1, 1, K)) * 0.02).astype(np.float16)
            w_log = rng.integers(0, 16, (N, K), dtype=np.uint8)
            qw = w_log[:, 0::2] | (w_log[:, 1::2] << 4)      # (N,K/2) 打包
            z_log = rng.integers(0, 8, (N, G), dtype=np.uint8)
            qz = z_log[:, 0::2] | (z_log[:, 1::2] << 4)      # (N,G/2)
            sc = (rng.uniform(0.001, 0.05, (N, G))).astype(np.float16)
            got = runners[("decode", case)].run(x, qw, sc, qz, max_tiles=2)
            want = op.matmul_nt_w4f16(x, w_log, sc, z_log)
            check(f"decode/{case}[0:64]", got[..., :64], want[..., :64],
                  atol=0.02)

        # prefill w4 matmul (_m16 store 修复变体): 2 tile 抽检 (全 N =
        # 128/344/128 发 × 分钟级, 不现实; 全量对拍见 _prefill_mm_verify.py
        # 3 case × 3 seeds 板上 PASS)
        for case, (_, K, _, _) in _MM_PREFILL_CASES.items():
            spec = iq_map.by_case(case, "prefill")
            N, G = spec.outputs[0].shape[-1], K // iq_map.GROUP
            x = (rng.standard_normal((1, 256, K)) * 0.02).astype(np.float16)
            w_log = rng.integers(0, 16, (N, K), dtype=np.uint8)
            qw = w_log[:, 0::2] | (w_log[:, 1::2] << 4)      # (N,K/2) 打包
            z_log = rng.integers(0, 8, (N, G), dtype=np.uint8)
            qz = z_log[:, 0::2] | (z_log[:, 1::2] << 4)      # (N,G/2)
            sc = (rng.uniform(0.001, 0.05, (N, G))).astype(np.float16)
            got = runners[("prefill", case)].run(x, qw, sc, qz, max_tiles=2)
            want = op.matmul_nt_w4f16(x, w_log, sc, z_log)
            check(f"prefill/{case}[0:64]", got[..., :64], want[..., :64],
                  atol=0.02)

        # 交错污染回归: 上面 matmul 已写 B 区; 再补 mul prefill (g1 大
        # 跨距) 与 rsqrt (LUT), 然后 lmhead 复跑 —— reload 机制下输出
        # 须与首次逐位一致 (板上 addtree 确定性)
        ins = [rng.uniform(-2.0, 2.0, t.shape).astype(np.float16)
               for t in iq_map.by_case("mul_gate", "prefill").inputs]
        runners[("prefill", "mul_gate")].run(*ins)
        runners[("decode", "rsqrt")].run(x1, mg)
        got2 = runners[("common", "lmhead_matmul_nt_f16")].run(lm_x, lm_w)
        check("interleave/lmhead 复跑逐位一致", got2, lm_got, atol=0.0)

    print("共 %d 项 FAIL" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        raise SystemExit(_selftest("--prefill-dual6" in sys.argv))
    print(__doc__)
