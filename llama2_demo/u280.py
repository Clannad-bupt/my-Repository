"""U280 板卡驱动接口.

负责人：周炜智

本模块同时承担两个角色:

1. **真实驱动** (``backend="xdma"``, 默认): 直接读写 Linux 设备节点,
   不依赖任何外部脚本或工具二进制. 硬件协议 (寄存器偏移、packer 时序、
   IQ 指令编码) 来源于 2026-09-21 在 zwz 服务器上对
   ``sub_lcs_fpga_20260908_134644`` case 的真机验证.
2. **Mock 后端** (``backend="mock"``): 用 bytearray 模拟 HBM, packer /
   START 只记录不动作, 让 Host 编排器 / 测试在没有板卡时也能对着真实
   接口契约开发. Mock 后端刻意不模拟计算.

关键契约 (调用方依赖, 改动需同步 test/test_u280.py):
- 算子 IQ 唯一形态 = **多 stage case 目录** (``stage*/`` +
  ``data_list_inst_subq{N}.txt`` + ``iq_inst_*.txt`` 时序交付, 如
  ``IQ/sample``). 2026-10-14 起单 hex plane-image 路径废弃
  (execute/adjust_addr 移除; llama2.py / test_iq_board.py 的
  ``execute(spec.iq, ...)`` 调用需迁移到句柄制, 属 zhiyue.chen 侧改动).
- 流程中读取 IQ **只传 int 句柄** (Host DDR 仓库地址), 不传文件路径,
  不传入整个 IQ File. 调用链::

      h  = u280.store_case(case_dir)                      # 读 data_list 入库
      h2 = u280.rebase_case_axi(h, in_addrs, out_addrs)   # 需要时派生新句柄
      u280.launch_case(h2)                                # 逐 stage 装载执行

- ``h2c`` / ``c2h`` / ``launch_case`` 均为**同步**接口: 返回即代表
  DMA 完成 / 全部 stage DONE. 异步实现必须在内部自旋等待.
- ``c2h`` 返回 ``bytes``, 长度严格等于 ``nbytes``.
- 仓库原件一经 ``store_case`` 登记逐字节不动; ``rebase_case_axi`` 一律
  以原件为底 patch 出**新句柄** (new 一个 IQ 进行修改).
- 所有参数错误抛 ``U280Error`` (本模块自定义异常), 不抛裸
  ``ValueError`` / ``AssertionError``, 方便上层统一捕获.
- 真实驱动打开失败时 ``init_device()`` 抛 ``U280Error``,
  调用方据此 CASE SKIP (板卡未就绪), 而非 CASE FAIL.

IQ 指令编码 (真机验证 2026-09-21 RTL_VERSION=0x00042200; 2026-10-12 经
SMI Spec V0.0.1 证实为官方定义, 见 §7.2.3 slot map 与 §3.3.4 操作码表):
- 94 通道的指令槽在 4KB 子队列平面内按 §7.2.3 紧打包 (变长 6B~22B,
  全表见常量 IQ_SLOT_MAP; LDB=[228,241]/14B, LDB_AXI=[242,261]/20B 与
  sub_lcs 包装头逐一吻合), 通道 i 的指令值 = 字节区间 [S_i, E_i] 的
  little-endian 拼接. 时序交付的同一通道可灌入多条指令 (每通道深度
  128), 这是 case 走 beat 级装载而非平面快照的根本原因.
- vld = 指令值 bit 0: vld=0 的指令不派发 (§8.8) — 既是 patch 的跳过
  判据, 也是 IQ_EN 推导的参与判据.
- gr_base_addr  = 指令值 bits [52:13], 40 bits (HBM 基址; 仅 AXI 变体
  STB_AXI(25)/LDB_AXI(27) 有此字段, 非 AXI 的 STB(24)/LDB(26) 用
  ur_addr 局部寻址, 不参与 FixAddr)
- gr_intlv_addr = 指令值 bits [92:53], 40 bits (SMC 间交错步长:
  SMC k 的数据位于 gr_base + k*gr_intlv; 生成器烘焙, rebase 不改写)
- IQ RAM 起始地址 0x1000; 装载 = beat 按交付顺序灌入, 写满某通道
  槽末拍 (BOFF_i = floor(E_i/16)*16) 触发该通道入队一条 (§8.2:
  iq_wri_addr={dec_sid, q_addr_cnt}, 批次完成后 axi_addr_clr 清零;
  每通道深度 128).

底层接口 (算子负责人约定, Demo方案): Init FPGA = init_device, H2C = h2c,
C2H = c2h, 存 IQ = store_case, FixAddr = rebase_case_axi,
Launch IQ = launch_case. (sub_lcs 编译器 case 的文件级 launch_iq 保留,
供 test_u280_board.py / test_u280_plane_board.py 回归基线使用.)

IQ 存储与执行模型 (Demo方案 数据交互图, 与 llama2.py 配套):
- 所有算子 IQ 先经 store_case 装载到 Host DDR 仓库, 返回 int 句柄;
  流程中读取 IQ 只传句柄 (地址), 不传入整个 IQ File.
- IQ 在仓库中按各 stage 的 data_list 顺序存储 (时序交付的原序);
  store 时若发现 LDB_AXI(27)/STB_AXI(25) 通道, 逐条记下其指令存储的
  原始 gr_base/gr_intlv (AXIAddrRef), 供需要时更新 IQ 微指令中的地址.
- 每次地址更新: rebase_case_axi 以仓库原件为底 patch 出**新句柄**,
  仓库原件逐字节不动.
- 装载: 逐 stage 把 beat 缓存经 BAR (IQP1 packer) 灌入片上 4KB 子队列
  平面, 然后 IQ_EN (按 vld 扫描推导) + START -> 等 DONE -> 清中断,
  再装下一 stage. HBM 上不驻留 IQ.

UNVERIFIED:
- R1 — rebase_case_axi 的"锚点平移"语义 (new_base = in/out_addrs[0] +
  (old_base − min_old_base), 保持生成器烘焙的流间相对布局, gr_intlv
  不改写): 位编码已经 2026-09-21 真机与 SPEC §3.3.4 操作码表定案;
  平移语义待 sample 减法算子真机验证 (test_u280_sample_board.py).

已 SPEC 定案 (原 UNVERIFIED):
- IQ_EN — §11.2.5: 本批参与 IQ 的掩码, START 时快照; §7.6.3: 所有
  使能 IQ 完成才 batch_done, 无超时. 正确值 = 本批程序实际装载的通道
  集合; 2026-10-13 定案 = 按各通道指令 vld (bit 0) 扫描推导, 并校验
  推导结果 ⊆ 本 build 已实现通道 (_iq_en_supported), 越界即报错.
  (2026-10-12 实验硬件否定旧约定"使能全部已实现类型": stage_1 使能
  含 stage_0 残留程序的 26/27, 重放旧程序致 CRD 错配挂死.)
  2026-10-13 真机 8/8 SMC 位精确 PASS 双路径互证: vld 推导 == IQ_ID
  推导 (sub_lcs 全 case; test_u280_plane_board.py).
"""

from __future__ import annotations

import copy
import mmap
import os
import re
import struct
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

# ---------------------------------------------------------------------------
# 异常
# ---------------------------------------------------------------------------


class U280Error(RuntimeError):
    """U280 驱动一切可预期错误的统一类型. 消息面向调用方 (人类可读)."""


# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------

# HBM 容量: U280 物理 8GB; 按 2^33 字节建模.
HBM_SIZE_BYTES = 1 << 33

# 默认 HBM 基址 (与历史版本兼容; 真实布局由 Host 编排器统一规划)
DEFAULT_IN_ADDRS = [0x0000_0000, 0x1000_0000, 0x2000_0000,
                    0x3000_0000, 0x4000_0000, 0x5000_0000, 0x6000_0000]
DEFAULT_OUT_ADDRS = [0x7000_0000]

# IQ 装载 beat 粒度 (128-bit)
IQ_ROW_BYTES = 16

# ---- AXI-Lite CSR 偏移 (BAR0) ----
CSR_CFG = 0x000          # [0]=enable, [1]=soft_reset
CSR_INT = 0x004          # [0]=intr, [1]=err, [15:8]=hw_intid, [23:16]=sw_intid
CSR_IQ_EN = [0x010, 0x014, 0x018, 0x01C]  # 128-bit IQ enable mask
CSR_START = 0x020        # [0]=start, [9:8]=subq
CSR_DONE = 0x024         # [0]=done
CSR_RTL_VERSION = 0x080  # RTL version register

# ---- IQP1 packer CSR 偏移 ----
PACKER_DATA = [0xF00, 0xF04, 0xF08, 0xF0C]  # 4x32-bit staging data
PACKER_WSTRB = 0xF10      # byte write strobe (16-bit)
PACKER_IQADDR = 0xF14     # target IQ RAM address
PACKER_CTRL = 0xF18       # [0]=commit, [1]=clear_error, [2]=clear_staging
PACKER_STATUS = 0xF1C     # see bit definitions below
PACKER_CAP = 0xF20        # capability: 0x49515031 = "IQP1"

PACKER_CTRL_COMMIT = 1 << 0
PACKER_CTRL_CLEAR_ERROR = 1 << 1
PACKER_CTRL_CLEAR_STAGING = 1 << 2

PACKER_STATUS_READY = 1 << 0
PACKER_STATUS_PENDING = 1 << 1
PACKER_STATUS_ANY_ERROR = 1 << 2
# bit[3]=incomplete, [4]=unaligned, [5]=bad_addr, [6]=zero_wstrb
PACKER_STATUS_STAGE_VALID_SHIFT = 8   # bits [13:8], 0x3F = all 6 fields staged
PACKER_STATUS_COMMIT_COUNT_SHIFT = 16  # bits [31:16], commit counter

PACKER_CAP_IQP1 = 0x49515031

# ---- SLV1 模式 (20261030_slv1_8g.bit 起) ----------------------------------
# packer 已从 RTL 删除: SMI 整体搬到 8G 之上的 SLV1 窗, BAR (M_AXI_LITE
# 32bit) 物理够不到 (旧地址读写全部 DECERR -> 读回 0xffffffff); 一切 CSR
# (版本探测/软复位/IQ_EN/START/INT/DONE 轮询/ack) 与 IQ 推送改走 XDMA
# SGDMA 通道节点 (pread/pwrite /dev/xdma0_c2h_0 | h2c_0, 每调用一条 TLP).
# 地址约定: CSR = SLV1_BASE + 窗内偏移 (寄存器表同 ISA v0.4.19, 未变);
#           IQ beat = SLV1_BASE + addr (addr 即旧 packer 的 IQ RAM 地址,
#           已含 subq 选择位 [13:12]). HBM 0-8G 数据语义完全不变.
# 判别 (init_device): SGDMA 读 0x080==0x00042200 且 0xF20==0.
#   旧 bit 上 0x2_0000_xxxx 回绕到 HBM 低地址 (bit33 被截), 读到的是数据
#   垃圾; 双 magic 与判据把误判概率压到可忽略.
SLV1_BASE = 0x2_0000_0000
SLV1_RTL_VERSION = 0x00042200

# ---- IQ 指令编码 (真机验证 + SMI Spec V0.0.1 定案) ----
IQ_WINDOW_BASE = 0x1000   # IQ RAM 起始地址
GR_BASE_BIT_LO = 13       # gr_base_addr = instr bits [52:13], 40 bits
GR_INTLV_BIT_LO = 53      # gr_intlv_addr = instr bits [92:53], 40 bits
FIELD_MASK40 = (1 << 40) - 1
INSTR_VLD_BIT = 0         # 指令 vld = 指令值 bit 0 (SPEC §3.3.4 各操作码表)

# ---- SMI Spec §7.2.3 slot map: 94 通道在 4KB 子队列平面内的紧打包布局 ----
# (2026-10-12 从 SPEC 内嵌表 §7.2.3 全表抽取, 与 sub_lcs 包装头逐一吻合)
# 每通道的指令槽 = 平面字节区间 [S_i, E_i] (含端点), 指令值 = 区间字节的
# little-endian 拼接. 装载时写满槽末拍 (BOFF_i = floor(E_i/16)*16) 触发
# 该通道入队一条 (§8.2). 布局分四段:
#   ID 0-23   计算/移位/UR 访问 (intadd/fp16mul/.../urrd0-3/urwr0-3)
#   ID 24-27  LSU: STB / STB_AXI / LDB / LDB_AXI — 仅 AXI 变体 (25/27)
#             携带 gr_base/gr_intlv 全局地址, 是 rebase 的 patch 对象
#   ID 28-91  RtoVR 输出选择 (6B/通道: S=262+6r, E=267+6r, r=ID-28)
#   ID 92     DS (Data Shuffle);  ID 93  INTLV
IQ_PLANE_BYTES = 0x1000   # 4KB 子队列平面 (SPEC §7.2.3)
IQ_SLOT_MAP: tuple[tuple[int, int], ...] = (
    (0, 7), (8, 13), (14, 20), (21, 26), (27, 33), (34, 39), (40, 46),
    (47, 53), (54, 60), (61, 67), (68, 74), (75, 81), (82, 88), (89, 95),
    (96, 117), (118, 123), (124, 131), (132, 139), (140, 147), (148, 155),
    (156, 165), (166, 175), (176, 185), (186, 195),          # ID 0-23
    (196, 208), (209, 227), (228, 241), (242, 261),          # ID 24-27 LSU
    *(((262 + 6 * r), (267 + 6 * r)) for r in range(64)),    # ID 28-91 RtoVR
    (646, 655), (656, 661),                                  # ID 92 DS / 93 INTLV
)
IQ_ID_STB, IQ_ID_STB_AXI = 24, 25
IQ_ID_LDB, IQ_ID_LDB_AXI = 26, 27
IQ_ID_DS, IQ_ID_INTLV = 92, 93
#: 携带 gr_base/gr_intlv 全局地址字段的 AXI 通道 (rebase 仅 patch 这些槽)
AXI_SLOT_IDS = (IQ_ID_STB_AXI, IQ_ID_LDB_AXI)

# ---- IQ_EN 使能 (语义与掩码来源均已 SPEC 定案) ----
# SMI Spec V0.0.1 定案的语义:
# - §11.2.5: IQ_EN = 本批**参与** IQ 的掩码; 写 SMI_IQ_START 时快照进
#   start FIFO, 执行用快照值. 正确值 = 本批程序实际装载的通道集合.
# - §7.6.3: 所有使能 IQ 各完成一次 (eid 匹配 sid) 才 batch_done,
#   **无超时机制** — 使能了程序无法完成的通道, 批次永远挂死.
# - §11.2.1: 超出 PARAM_SMI_IQ_CNT 的高位写入时被硬件屏蔽 (读回 0) —
#   这正是 2026-10-12 探测到的现象 (写 0xffffffff 读回 0x30000077),
#   属 SPEC 记载的正常行为, 非常规 bug. 本 build 实测可写位图
#   _iq_en_supported = 0x0000000030000077ffffffffffffffff
#   (类型 0-66/68-70/92/93; SPEC §3.3.5 注明 IQ71-91 无 o_cru_* 输出,
#   未实现与 SPEC 一致; IQ67 SPEC 标注在打包范围内但本 build 不可使能,
#   待与 RTL 属主确认).
# 掩码来源 (2026-10-13 定案, 采纳候选 b): 按各通道槽内指令 vld (bit 0)
# 扫描推导 — case 路径在 store_case 时逐 stage 推导 (vld=0 的指令不
# 派发 (§8.8), 对应通道即使被使能也会立即完成 (2026-10-12 真机
# stage_0 证据), 故 vld 扫描精确且天然安全).
# 2026-10-12 真机实验 (test_u280_iqen_board.py) 按上述语义复核:
# - 使能全部已实现类型: stage_0 DONE (未装载通道程序为空, 立即完成),
#   stage_1 **超时** (INT=0x00010100, DONE=0) — 通道 26/27 残留 stage_0
#   的程序, 被使能后重放旧装载, 与本批并发流量 CRD 错配 (§4.2.3/§7.8),
#   报错且批次永不完成. "使能全部已实现类型"被硬件证据否定.
# IQ_ENABLE_ALL 角色: 探测写入值 + mock 默认.
IQ_ENABLE_ALL = (1 << 128) - 1

# ---- 设备节点 ----
DEFAULT_USER_DEV = "/dev/xdma0_user"
DEFAULT_H2C_DEV = "/dev/xdma0_h2c_0"
DEFAULT_C2H_DEV = "/dev/xdma0_c2h_0"
DEFAULT_BAR_SIZE = 0x10000

# ---- DMA 分块 ----
RW_MAX_SIZE = 0x7FFFF000  # XDMA 驱动单次 write()/read() 上限 (~2GB - 4K)

# ---------------------------------------------------------------------------
# IQ 地址字段定义 (编码参考文档; 当前代码路径未直接消费, 保留备查)
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class IQAddrField:
    """一条 IQ 指令中地址字段的位置定义.

    字段语义由 RTL 微架构定义, 当前基于 2026-09-21 真机验证的编码.
    """

    bit_lo: int            # 字段在指令值中的起始 bit
    nbits: int             # 字段宽度
    description: str = ""


#: LDB_AXI (IQ_ID=27) 的地址字段: gr_base + gr_intlv
LDB_AXI_FIELDS = (
    IQAddrField(GR_BASE_BIT_LO, 40, "gr_base_addr"),
    IQAddrField(GR_INTLV_BIT_LO, 40, "gr_intlv_addr"),
)

#: STB_AXI (IQ_ID=25) 的地址字段: gr_base + gr_intlv
STB_AXI_FIELDS = (
    IQAddrField(GR_BASE_BIT_LO, 40, "gr_base_addr"),
    IQAddrField(GR_INTLV_BIT_LO, 40, "gr_intlv_addr"),
)

# ---------------------------------------------------------------------------
# IQ 指令文件 (case 时序交付格式) 解析
# ---------------------------------------------------------------------------
#
# case 交付的 iq_inst_*.txt 结构 (真机验证 2026-09-21):
#   第 1 行: IQ_ID=N IQ_BITS=N IQ_BYTE_START=N IQ_BYTE_END=N ...
#   后续行:  INST=<hex> ADDR=<hex> BEAT=<hex> DATA=<32 hex>
# 同一 INST 的连续若干行构成一条指令; 每行是一个 16B beat:
#   beat 的 IQ RAM 地址 = ADDR (绝对地址, >= 0x1000)
#   beat 的字节 i (IQ RAM ADDR+i) = DATA hex 的 pair (15-i), 即 DATA 高字节在前.
# 指令值 = IQ RAM [0x1000+IQ_BYTE_START .. 0x1000+IQ_BYTE_END] 的
# little-endian 拼接; gr_base/gr_intlv 字段按上面的 bit 位置从中提取/回写.

_IQ_ID_RE = re.compile(r"\bIQ_ID=(\d+)\b")
_IQ_HDR_FIELD_RES = {
    "IQ_BITS": re.compile(r"\bIQ_BITS=(\d+)\b"),
    "IQ_BYTE_START": re.compile(r"\bIQ_BYTE_START=(\d+)\b"),
    "IQ_BYTE_END": re.compile(r"\bIQ_BYTE_END=(\d+)\b"),
}
_IQ_WRITE_RE = re.compile(
    r"INST=(?P<inst>[0-9a-fA-F]+)\s+ADDR=(?P<addr>[0-9a-fA-F]+)\s+"
    r"BEAT=(?P<beat>[0-9a-fA-F]+)\s+DATA=(?P<data>[0-9a-fA-F]+)")


@dataclass(frozen=True)
class IQBeat:
    """一条 16B 装载 beat (写 IQ RAM 的最小单位).

    data_hex 保持文件原样 (32 个 hex 字符, 高字节在前); 装载时反转为
    小端后经 IQP1 packer 写入.
    """

    addr: int        # IQ RAM 绝对地址 (16B 对齐, >= IQ_WINDOW_BASE)
    mask: int        # 字节写选通 (16-bit, 与 DATA pair 对齐)
    data_hex: str    # 32 hex chars, 高字节在前


@dataclass
class IQInstruction:
    """一条 IQ 指令: 若干连续 beat + 解码出的指令值与地址字段."""

    inst: int                    # INST= 字段 (16 进制)
    beats: tuple[IQBeat, ...]    # 原始 beat (按文件顺序)
    value: int                   # 指令值 ([bstart..bend] little-endian)
    base_addr: int               # gr_base_addr (40b)
    intlv_addr: int              # gr_intlv_addr (40b)
    _ram: dict = field(default_factory=dict, repr=False)  # {绝对 IQ 地址: 字节}

    def _reencode_beats(self) -> None:
        """用当前 _ram 内容重建 beats (地址改写后调用)."""
        new_beats = []
        for b in self.beats:
            h = bytearray(16)
            for i in range(16):
                if (b.mask >> i) & 1:
                    h[15 - i] = self._ram.get(b.addr + i, 0)
            new_beats.append(IQBeat(b.addr, b.mask, h.hex()))
        self.beats = tuple(new_beats)


@dataclass
class IQFile:
    """一份 case 交付的 IQ 指令文件 (iq_inst_*.txt)."""

    path: str
    iq_id: int                   # 0..127, 用于 IQ_EN mask
    iq_bits: int
    byte_start: int              # IQ_BYTE_START (相对 IQ_WINDOW_BASE)
    byte_end: int                # IQ_BYTE_END
    instructions: list           # [IQInstruction], 按文件顺序

    @classmethod
    def load(cls, path) -> "IQFile":
        """从 iq_inst_*.txt 解析出 header 与全部指令.

        解析失败抛 U280Error (面向调用方的人类可读消息).
        """
        p = Path(path)
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            raise U280Error(f"无法读取 IQ 文件 {p}: {exc}") from exc
        lines = text.splitlines()
        if not lines:
            raise U280Error(f"IQ 文件为空: {p}")
        first = lines[0]
        m = _IQ_ID_RE.search(first)
        if m is None:
            raise U280Error(f"IQ 文件缺 IQ_ID header: {p}")
        iq_id = int(m.group(1))
        fields = {}
        for name, rx in _IQ_HDR_FIELD_RES.items():
            mm = rx.search(first)
            if mm is None:
                raise U280Error(f"IQ 文件 header 缺 {name}: {p}")
            fields[name] = int(mm.group(1))
        bstart, bend = fields["IQ_BYTE_START"], fields["IQ_BYTE_END"]

        # 按 INST 分组 (连续的同一 INST 属一条指令)
        groups: list[IQInstruction] = []
        for raw in lines[1:]:
            mm = _IQ_WRITE_RE.search(raw.strip())
            if not mm:
                continue
            inst = int(mm.group("inst"), 16)
            beat = IQBeat(int(mm.group("addr"), 16),
                          int(mm.group("beat"), 16),
                          mm.group("data").zfill(32))
            if not groups or groups[-1].inst != inst:
                groups.append(IQInstruction(inst, (beat,), 0, 0, 0))
            else:
                groups[-1].beats += (beat,)

        if not groups:
            raise U280Error(f"IQ 文件没有指令行: {p}")

        lo = IQ_WINDOW_BASE + bstart
        hi = IQ_WINDOW_BASE + bend
        for ins in groups:
            ram = {}
            for b in ins.beats:
                h = bytes.fromhex(b.data_hex)
                if len(h) != 16:
                    raise U280Error(
                        f"IQ DATA 不是 16B: {p} inst {ins.inst} beat @{b.addr:#x}")
                for i in range(16):
                    if (b.mask >> i) & 1:
                        ram[b.addr + i] = h[15 - i]
            ins._ram = ram
            bs = bytes(ram.get(a, 0) for a in range(lo, hi + 1))
            v = int.from_bytes(bs, "little")
            ins.value = v
            ins.base_addr = (v >> GR_BASE_BIT_LO) & FIELD_MASK40
            ins.intlv_addr = (v >> GR_INTLV_BIT_LO) & FIELD_MASK40

        return cls(path=str(p), iq_id=iq_id, iq_bits=fields["IQ_BITS"],
                   byte_start=bstart, byte_end=bend, instructions=groups)

    @property
    def _lo(self) -> int:
        return IQ_WINDOW_BASE + self.byte_start

    @property
    def _hi(self) -> int:
        return IQ_WINDOW_BASE + self.byte_end

    def beats(self) -> list[IQBeat]:
        """展开为按文件顺序的 beat 列表 (装载用)."""
        out: list[IQBeat] = []
        for ins in self.instructions:
            out.extend(ins.beats)
        return out


def _patch_axi_fields(iqfile: IQFile, ins: IQInstruction,
                      new_base: int | None = None,
                      new_intlv: int | None = None) -> None:
    """把单条指令的 gr_base / gr_intlv 改写为新值 (其余位不动, beats 同步重建).

    两个字段独立可选, 传 None 的字段保持原值.
    仅供 rebase_case_axi 在**副本**上调用; 仓库原件不得传入.
    """
    if new_base is not None and not 0 <= new_base <= FIELD_MASK40:
        raise U280Error(f"gr_base 超出 40b: {new_base!r}")
    if new_intlv is not None and not 0 <= new_intlv <= FIELD_MASK40:
        raise U280Error(f"gr_intlv 超出 40b: {new_intlv!r}")
    v = ins.value
    if new_base is not None:
        v = ((v & ~(FIELD_MASK40 << GR_BASE_BIT_LO))
             | ((new_base & FIELD_MASK40) << GR_BASE_BIT_LO))
    if new_intlv is not None:
        v = ((v & ~(FIELD_MASK40 << GR_INTLV_BIT_LO))
             | ((new_intlv & FIELD_MASK40) << GR_INTLV_BIT_LO))
    if v == ins.value:
        return
    nbytes = iqfile._hi - iqfile._lo + 1
    bs = v.to_bytes(nbytes, "little")
    for j, byte in enumerate(bs):
        ins._ram[iqfile._lo + j] = byte
    ins.value = v
    if new_base is not None:
        ins.base_addr = new_base & FIELD_MASK40
    if new_intlv is not None:
        ins.intlv_addr = new_intlv & FIELD_MASK40
    ins._reencode_beats()


# ---------------------------------------------------------------------------
# case 仓库 (多 stage 算子唯一 IQ 形态; Host DDR 驻留, 句柄寻址)
# ---------------------------------------------------------------------------

# stage 目录命名: stage0/stage1/... 或 stage_0/stage_1/... (数字排序)
_STAGE_DIR_RE = re.compile(r"^stage_?(\d+)$")


@dataclass(frozen=True)
class AXIAddrRef:
    """一条 AXI 指令在仓库存储中的位置 + 它存储的原始地址 (store 时记下).

    "如果有 ldb_axi 和 stb_axi, 就记下 IQ 存储的地址" — 即本结构:
    (stage, entry, inst) 三级定位 + 指令内烘焙的原始 gr_base/gr_intlv,
    rebase 时按 ref 定位到副本中的对应指令做位手术, 无需重新解析文件.
    """

    stage_idx: int    # 第几个 stage (data_list 发现顺序)
    entry_idx: int    # stage 内 data_list 第几行
    inst_idx: int     # 文件内第几条指令
    old_base: int     # 指令内存储的原始 gr_base
    old_intlv: int    # 指令内存储的原始 gr_intlv


@dataclass(frozen=True)
class _StageEntry:
    """data_list 一行的仓库快照 (原件只读)."""

    mem_name: str              # data_list 第一列 (诊断用)
    iq_id: int
    iqfile: IQFile             # 解析快照, 仓库内只读
    beats: tuple[IQBeat, ...]  # 该文件的 beat 序列 (装载缓存)


@dataclass(frozen=True)
class _StageRecord:
    """一个 stage 的完整装载单元."""

    entries: tuple[_StageEntry, ...]  # 严格按 data_list 顺序
    beats: tuple[IQBeat, ...]         # entries 顺序拼接 (packer 直接消费)
    iq_mask: int                      # vld 扫描推导 (SPEC §11.2.5)


@dataclass(frozen=True)
class _CaseEntry:
    """一个多 stage 算子 case 的仓库条目 (int 句柄指向它)."""

    case_dir: str
    subq: int
    stages: tuple[_StageRecord, ...]
    ldb_axi: tuple[AXIAddrRef, ...]   # 空 tuple = 该 case 无 LDB_AXI
    stb_axi: tuple[AXIAddrRef, ...]   # 空 tuple = 该 case 无 STB_AXI


def _discover_stage_dirs(case_dir: Path) -> list[Path]:
    """发现 case 下的 stage 目录, 按名字尾号数字排序."""
    found = []
    for p in case_dir.iterdir():
        if p.is_dir():
            m = _STAGE_DIR_RE.match(p.name)
            if m:
                found.append((int(m.group(1)), p))
    if not found:
        raise U280Error(f"case 目录下没有 stage 目录 (stage*/): {case_dir}")
    found.sort(key=lambda t: t[0])
    return [p for _, p in found]


def _parse_data_list(stage_dir: Path, subq: int) -> list[tuple[str, Path]]:
    """读 data_list_inst_subq{subq}.txt, 返回 [(mem_name, 文件路径)] 按行序.

    data_list 缺失时回退为 iq_inst_*_subq{subq}.txt 文件名排序;
    data_list 点名但文件缺失是交付错误, 抛 U280Error (不静默跳过).
    """
    dl = stage_dir / f"data_list_inst_subq{subq}.txt"
    if not dl.is_file():
        files = sorted(stage_dir.glob(f"iq_inst_*_subq{subq}.txt"))
        if not files:
            raise U280Error(
                f"{stage_dir} 既无 data_list_inst_subq{subq}.txt "
                f"也无 iq_inst_*_subq{subq}.txt")
        return [(p.stem, p) for p in files]
    out: list[tuple[str, Path]] = []
    for raw in dl.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) < 2:
            raise U280Error(f"data_list 行格式错误: {dl}: {line!r}")
        p = stage_dir / Path(parts[1]).name
        if not p.is_file():
            raise U280Error(f"data_list 点名的文件缺失: {p} (出自 {dl})")
        out.append((parts[0], p))
    if not out:
        raise U280Error(f"data_list 为空: {dl}")
    return out


# ---------------------------------------------------------------------------
# 驱动主体
# ---------------------------------------------------------------------------


class U280:
    """U280 板卡驱动接口.

    用法 (句柄制多 stage case)::

        u280 = U280()                # 或 U280(backend="mock") 无板卡开发
        h = u280.store_case("IQ/sample")     # 入库, 得句柄 (Host DDR 地址)
        u280.init_device()
        u280.h2c(data, addr, len(data))
        h2 = u280.rebase_case_axi(h, [in_addr], [out_addr])  # 派生新句柄
        u280.launch_case(h2)                 # 逐 stage 装载并执行
        out = u280.c2h(out_addr, nbytes)

    仓库原件 (h) 任何路径都不改写; 每次地址更新从 h 派生新句柄.
    后端选择:
    - ``"xdma"`` (默认): 真实 XDMA 驱动, 直接读写 /dev/xdma0_*.
    - ``"mock"``: 内存模拟 HBM, packer/START 仅记录 (_mock_packer_log /
      _mock_start_log), 仅用于打通流程, 不做数值计算.
    """

    def __init__(self, backend: str = "xdma",
                 user_dev: str = DEFAULT_USER_DEV,
                 h2c_dev: str = DEFAULT_H2C_DEV,
                 c2h_dev: str = DEFAULT_C2H_DEV,
                 bar_size: int = DEFAULT_BAR_SIZE,
                 timeout_s: float = 10.0,
                 poll_s: float = 0.01,
                 csr_settle_s: float = 0.001,
                 reset_settle_s: float = 1.0,
                 dma_align: int = 4096):
        if backend not in ("xdma", "mock"):
            raise U280Error(
                f"未知 backend {backend!r}; 可选 'xdma' (真实驱动) 或 'mock' (内存模拟)")
        self.backend = backend
        self.user_dev = user_dev
        self.h2c_dev = h2c_dev
        self.c2h_dev = c2h_dev
        self.bar_size = bar_size
        self.timeout_s = timeout_s
        self.poll_s = poll_s
        self.csr_settle_s = csr_settle_s
        self.reset_settle_s = reset_settle_s
        self.dma_align = dma_align

        self.in_addrs = list(DEFAULT_IN_ADDRS)
        self.out_addrs = list(DEFAULT_OUT_ADDRS)

        # case 仓库 (Host DDR): 句柄 -> _CaseEntry. 只增不改;
        # rebase 产出新句柄, 原句柄条目逐字节不动.
        self._case_registry: dict[int, _CaseEntry] = {}
        self._next_case_handle = 1

        # IQ_EN 已实现类型位图: xdma init_device 时探测 (写全 1 读回);
        # 未探测 (mock) 时为 IQ_ENABLE_ALL.
        self._iq_en_supported = IQ_ENABLE_ALL

        # 真实驱动资源
        self._bar_mm: mmap.mmap | None = None
        self._bar_fd: int | None = None
        self._h2c_fd: int | None = None
        self._c2h_fd: int | None = None

        # mock 后端资源
        self._hbm: bytearray | None = None
        self._mock_packer_log: list[tuple[int, int, bytes]] = []
        self._mock_start_log: list[tuple[int, int]] = []

        # SLV1 模式标志: init_device 探测置位 (仅真实驱动; mock 恒 False).
        # True = 20261030+ 无 packer 新 bit, CSR/IQ 全走 SGDMA 直推.
        self._slv1 = False
        # slv1 流推累积: 本 stage 的 IQ RAM 镜像 {addr: byte} (交付 beat
        # 是 packer 写轨迹, 同槽反复重写, last-write-wins) 与其 subq 窗
        # 基址; _packer_write_beat 累积, _start_and_wait 冲刷 (run 封顶
        # 64B 一笔一写).
        self._slv1_iq_img = []
        self._slv1_iq_base = 0
        # slv1 流推单笔长度上限: 64B 长度实证安全 (1422B 单写背压挂桥);
        # 注意 A.6 勘误: 安全的是"长度", 从非 16B 对齐起点切 64B 不安全
        # (BAD-C) —— 由 _slv1_flush_iq 合并封顶保证切块循环永不真切.
        self._slv1_chunk = 64
        # 优化① run 计划直推: board_runners._launch_case_tb 在 store_case
        # 后一次性把 beats 拼装成 A.4 合并 runs 缓存, 发射时置此字段,
        # _slv1_flush_iq 直接逐 run pwrite (省每 launch 的逐 beat 拼装+
        # 合并; 写计划与逐 beat 路径逐字节相同, 语义不变).
        self._slv1_preplan: tuple[int, list] | None = None
        # 优化③ CSR 快档: 2026-11-02 板上判死, 已移除. 三连实证:
        # (a) 单笔 16B pwrite 跨 4 寄存器写 CSR 区直接挂死 H2C 通道并
        #     拖死整机 (2026-10-31, ssh banner 无响应仅 ICMP 存活);
        # (b) 单笔 16B pread 读 CSR 区静默返回 "word0 正确 + 其余清零"
        #     (无副作用, 但与 4B 读实测不符) — CSR 窗只支持 ≤4B 单事务,
        #     读写双向皆然;
        # (c) 4 笔背靠背 4B 裸写 IQ_EN 会静默丢中间字 (实测 word2 丢失,
        #     word0/1 落地; 慢速路径逐字读回从未失配) — 从机丢字不破坏
        #     后续到达, 任何 canary/统验重排序都证不了前序全落, 唯一
        #     可靠形态 = 逐字 "写+读回" (= 原慢速路径), ③ 无任何可省.
        self._slv1_fast_csr = False

        self._initialized = False

    # ------------------------------------------------------------------
    # 生命周期
    # ------------------------------------------------------------------

    def _reset_smi_hw(self) -> None:
        """SMI 软复位 + 复位后状态校验 (寄存器级, 不做 _check_ready).

        写 CFG[1] 软复位, 等 reset_settle_s 后校验 CFG/INT/DONE 全 0
        且 packer 回到 READY (slv1 模式无 packer, 跳过该检查).
        init_device 步骤 5 与卡死恢复共用.
        """
        self._bar_write32(CSR_CFG, 0x2)
        time.sleep(self.reset_settle_s)
        # slv1 流推: 丢弃未冲刷的残流 (异常中断的 stage 残留), 防污染
        # 下一次发射 / 触发跨窗混杂误报.
        self._slv1_iq_img = []
        self._slv1_preplan = None
        cfg = self._bar_read32(CSR_CFG)
        int_word = self._bar_read32(CSR_INT)
        done_word = self._bar_read32(CSR_DONE)
        if cfg != 0 or int_word != 0 or done_word != 0:
            raise U280Error(
                f"SMI 软复位失败: CFG=0x{cfg:08x} INT=0x{int_word:08x} "
                f"DONE=0x{done_word:08x}")
        if self._slv1:
            return
        packer_status = self._bar_read32(PACKER_STATUS)
        if packer_status != PACKER_STATUS_READY:
            raise U280Error(
                f"IQ packer 复位后状态异常: 0x{packer_status:08x}, "
                f"期望 READY (0x{PACKER_STATUS_READY:08x})")

    def reset_smi(self) -> None:
        """SMI 软复位 (公开入口): 算子间清场 / 出错后恢复板卡用.

        联调连续跑多个算子时, 一个算子异常后调用本方法把 SMI 拉回
        干净状态再继续下一个, 无需重新 init_device. mock 后端为空操作.
        """
        self._check_ready()
        if self.backend == "mock":
            return
        self._reset_smi_hw()

    def init_device(self) -> None:
        """初始化设备: 上电 / 加载驱动 / 软复位 / 验证版本.

        真实驱动: 打开 BAR + H2C + C2H 设备节点, 读 RTL_VERSION,
        验证 IQP1 packer, 执行 SMI 软复位. 失败抛 ``U280Error``.
        Mock: 分配模拟 HBM. 重复调用为幂等 (直接返回).
        """
        if self._initialized:
            return

        if self.backend == "mock":
            self._hbm = bytearray(min(HBM_SIZE_BYTES, 4 * 1024**3))
            self._initialized = True
            return

        # ---- 真实驱动 ----
        try:
            # 1. 打开 DMA 通道 (HBM 数据与 SGDMA CSR/IQ 探测都走这两个节点)
            self._h2c_fd = os.open(self.h2c_dev, os.O_RDWR)
            self._c2h_fd = os.open(self.c2h_dev, os.O_RDWR)

            # 2. 探测 SLV1 新 bit (20261030+, packer 已删, SMI 窗 @ 8G 之上
            #    仅 SGDMA 可达). 旧 bit 上该地址段回绕到 HBM 低址, 读到的是
            #    数据垃圾 —— magic 0x42200 防伪. 2026-10-30 第四版 bit 起
            #    0xF20 (PACKER_CAP) 读取挂死 (寄存器已删, 从机不应答),
            #    判据收敛为单 magic, 0xF20 一律不读.
            if self._sg_read32(CSR_RTL_VERSION) == SLV1_RTL_VERSION:
                self._slv1 = True

            if not self._slv1:
                # 旧 bit (v7 及以前): 打开 BAR (mmap AXI-Lite);
                # O_SYNC 仅 Linux 提供, 缺省为 0
                self._bar_fd = os.open(self.user_dev, os.O_RDWR | getattr(os, "O_SYNC", 0))
                self._bar_mm = mmap.mmap(
                    self._bar_fd, self.bar_size,
                    mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE,
                    offset=0)

                # 验证 RTL 版本 (不硬编码期望值, 只读; 由调用方决定是否匹配)
                rtl_version = self._bar_read32(CSR_RTL_VERSION)

                # 验证 IQP1 packer
                packer_cap = self._bar_read32(PACKER_CAP)
                if packer_cap != PACKER_CAP_IQP1:
                    raise U280Error(
                        f"IQ packer CAP=0x{packer_cap:08x}, 期望 0x{PACKER_CAP_IQP1:08x} (IQP1)")

            # 3. SMI 软复位 (slv1 下经 SGDMA, 且跳过 packer READY 检查)
            self._reset_smi_hw()

            # 4. 探测 IQ_EN 已实现类型位图: 未实现类型的使能位只读 0
            # (2026-10-12 真机: 写 0xffffffff 读回 0x30000077 等);
            # 无论最终使能约定如何, 写入值都必须落在该位图内.
            supported = 0
            for idx, off in enumerate(CSR_IQ_EN):
                self._bar_write32(off, 0xFFFFFFFF)
                supported |= self._bar_read32(off) << (32 * idx)
                self._bar_write32(off, 0x0)
            self._iq_en_supported = supported

            self._initialized = True

        except OSError as exc:
            self.close()
            raise U280Error(
                f"打开 U280 设备失败: {exc}. "
                f"请确认 XDMA 驱动已加载且当前用户有 /dev/xdma0_* 权限."
            ) from exc

    def close(self) -> None:
        """释放设备资源. 重复调用幂等. case 仓库 (Host 侧) 不随 close 清除."""
        for attr in ("_bar_mm", "_bar_fd", "_h2c_fd", "_c2h_fd"):
            res = getattr(self, attr)
            if res is not None:
                try:
                    if attr == "_bar_mm":
                        res.close()
                    else:
                        os.close(res)
                except OSError:
                    pass
                setattr(self, attr, None)
        self._hbm = None
        self._initialized = False

    def __enter__(self) -> "U280":
        self.init_device()
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # ------------------------------------------------------------------
    # BAR 读写 (真实驱动)
    # ------------------------------------------------------------------

    def _bar_check(self, off: int, n: int) -> None:
        if off < 0 or off + n > self.bar_size:
            raise U280Error(
                f"BAR 访问越界: off=0x{off:x} size={n} bar_size=0x{self.bar_size:x}")

    # ------------------------------------------------------------------
    # SGDMA CSR (SLV1 模式: 20261030+ bit, packer 已删, 一切 CSR 走通道节点)
    # ------------------------------------------------------------------

    def _sg_read32(self, off: int) -> int:
        assert self._c2h_fd is not None
        data = os.pread(self._c2h_fd, 4, SLV1_BASE + off)
        if len(data) != 4:
            raise U280Error(
                f"SGDMA CSR 读失败 @0x{off:03x}: {len(data)}/4 字节")
        return struct.unpack("<I", data)[0]

    def _sg_write32(self, off: int, val: int) -> None:
        assert self._h2c_fd is not None
        n = os.pwrite(self._h2c_fd, struct.pack("<I", val & 0xFFFFFFFF),
                      SLV1_BASE + off)
        if n != 4:
            raise U280Error(f"SGDMA CSR 写失败 @0x{off:03x}: {n}/4 字节")
        _ = self._sg_read32(off)  # 读回排序 posted write (同 BAR 惯例)

    def _bar_read32(self, off: int) -> int:
        if self._slv1:
            return self._sg_read32(off)
        self._bar_check(off, 4)
        assert self._bar_mm is not None
        return struct.unpack_from("<I", self._bar_mm, off)[0]

    def _bar_write32(self, off: int, val: int) -> None:
        if self._slv1:
            self._sg_write32(off, val)
            return
        self._bar_check(off, 4)
        assert self._bar_mm is not None
        struct.pack_into("<I", self._bar_mm, off, val & 0xFFFFFFFF)
        # PCIe BAR MMIO 不支持 msync; 读回排序 posted write
        _ = self._bar_mm[off]

    def _bar_write_checked(self, off: int, val: int, name: str = "") -> None:
        """写 CSR 并读回验证."""
        self._bar_write32(off, val)
        if self.csr_settle_s > 0:
            time.sleep(self.csr_settle_s)
        actual = self._bar_read32(off)
        if actual != (val & 0xFFFFFFFF):
            raise U280Error(
                f"CSR 写回不匹配 {name} @0x{off:03x}: "
                f"写入=0x{val & 0xFFFFFFFF:08x} 读回=0x{actual:08x}")

    # ------------------------------------------------------------------
    # 参数校验 (真实/mock 后端共用)
    # ------------------------------------------------------------------

    def _check_ready(self) -> None:
        if not self._initialized:
            raise U280Error("设备未初始化: 请先调用 init_device()")

    def _check_addr(self, addr: int, nbytes: int) -> None:
        if not isinstance(addr, int) or addr < 0:
            raise U280Error(f"HBM 地址非法: {addr!r} (应为非负整数)")
        if not isinstance(nbytes, int) or nbytes <= 0:
            raise U280Error(f"字节数非法: {nbytes!r} (应为正整数)")
        if addr + nbytes > HBM_SIZE_BYTES:
            raise U280Error(
                f"地址越界: 0x{addr:X} + {nbytes} 超出 HBM 容量 0x{HBM_SIZE_BYTES:X}")

    # ------------------------------------------------------------------
    # DMA 数据传输
    # ------------------------------------------------------------------

    def h2c(self, src_data: bytes, hbm_addr: int, nbytes: int) -> None:
        """Host -> Card: 把 ``src_data`` 前 ``nbytes`` 字节写入 HBM.

        同步语义: 返回时数据已对后续 launch_case() 可见.
        """
        self._check_ready()
        self._check_addr(hbm_addr, nbytes)
        if not isinstance(src_data, (bytes, bytearray, memoryview)):
            raise U280Error(f"src_data 必须是 bytes-like, 实际 {type(src_data).__name__}")
        if len(src_data) < nbytes:
            raise U280Error(
                f"src_data 长度 {len(src_data)} < 请求写入 {nbytes} 字节")
        data = bytes(src_data[:nbytes])

        if self.backend == "mock":
            assert self._hbm is not None
            self._hbm[hbm_addr:hbm_addr + nbytes] = data
            return

        # ---- 真实驱动: write() 到 /dev/xdma0_h2c_0 ----
        assert self._h2c_fd is not None
        buf = data
        offset = hbm_addr
        count = 0
        while count < nbytes:
            chunk = min(nbytes - count, RW_MAX_SIZE)
            try:
                os.lseek(self._h2c_fd, offset, os.SEEK_SET)
                written = os.write(self._h2c_fd, buf[count:count + chunk])
            except OSError as exc:
                raise U280Error(f"H2C DMA 失败 @0x{offset:X}: {exc}") from exc
            if written != chunk:
                raise U280Error(
                    f"H2C DMA 下溢 @0x{offset:X}: {written}/{chunk} 字节")
            count += written
            offset += written

    def c2h(self, hbm_addr: int, nbytes: int) -> bytes:
        """Card -> Host: 从 HBM 读 ``nbytes`` 字节, 返回 ``bytes``.

        同步语义: 返回时数据已完整读回 Host 内存.
        """
        self._check_ready()
        self._check_addr(hbm_addr, nbytes)

        if self.backend == "mock":
            assert self._hbm is not None
            return bytes(self._hbm[hbm_addr:hbm_addr + nbytes])

        # ---- 真实驱动: read() 从 /dev/xdma0_c2h_0 ----
        assert self._c2h_fd is not None
        buf = bytearray(nbytes)
        offset = hbm_addr
        count = 0
        while count < nbytes:
            chunk = min(nbytes - count, RW_MAX_SIZE)
            try:
                os.lseek(self._c2h_fd, offset, os.SEEK_SET)
                read = os.read(self._c2h_fd, chunk)
            except OSError as exc:
                raise U280Error(f"C2H DMA 失败 @0x{offset:X}: {exc}") from exc
            if len(read) != chunk:
                raise U280Error(
                    f"C2H DMA 下溢 @0x{offset:X}: {len(read)}/{chunk} 字节")
            buf[count:count + chunk] = read
            count += len(read)
            offset += len(read)
        return bytes(buf)

    # ------------------------------------------------------------------
    # IQ 装载 (IQP1 packer)
    # ------------------------------------------------------------------

    def _packer_write_beat(self, addr: int, beat_mask: int, data: bytes) -> None:
        """写入一条 128-bit IQ beat (旧 bit 走 IQP1 packer; slv1 直推 SGDMA).

        addr: IQ RAM 目标地址 (16B 对齐, [0x1000, 0x5000), 含 subq 位 [13:12])
        beat_mask: 字节写选通 (16-bit, 连续段 [lo:hi])
        data: 16 字节, 小端 (IQ RAM 低地址在前)

        mock 后端: 校验后仅记录到 _mock_packer_log, 不动作.
        """
        if addr & 0xF:
            raise U280Error(f"IQ packer 目标未 16B 对齐: 0x{addr:08x}")
        if addr < IQ_WINDOW_BASE or addr >= 0x5000:
            raise U280Error(f"IQ packer 目标超出 IQ 窗口: 0x{addr:08x}")
        if beat_mask == 0 or beat_mask > 0xFFFF:
            raise U280Error(f"IQ packer WSTRB 非法: 0x{beat_mask:x}")
        if len(data) != 16:
            raise U280Error(f"IQ DATA 不是 16B: {data.hex()}")

        if self.backend == "mock":
            self._mock_packer_log.append((addr, beat_mask, bytes(data)))
            return

        if self._slv1:
            # slv1 IQ 推送 = 保序队列追加模型 (2026-11-01 板上实证 +
            # RTL 附录 A TEST5 验收):
            # 交付 beat 序列是 packer 写轨迹, 同一槽位被反复写不是重写
            # 而是**队列追加** (stage_1 单槽最多 26 条指令 = 队列深度,
            # 指令自带长度字段自行分帧); packer 时代 q_addr_cnt 逐 commit
            # 自增追加, Rev4 直推路径 commit 触发 = **携槽尾块的拍**
            # (RTL 附录 A.6 勘误: 一事务可 commit 多槽 —— TEST3 单事务
            # 跨槽 [196,262) commit 4 槽; 只写槽头的事务 commit 0 次;
            # 追加/同址重写/多槽合并 burst 均不受限)。
            # 因此此处**保序**累积每 beat 的有效字节段 (10-30 的
            # last-write-wins 镜像模型是 BAD-B 零输出根因: 多指令槽
            # 只执行最后一条, LUT 加载指令被 x 加载覆盖)。
            # flush 时仅合并严格前向相邻段, 逐段在真实偏移推入。
            lo = (beat_mask & -beat_mask).bit_length() - 1
            hi = beat_mask.bit_length() - 1
            if beat_mask != (((1 << (hi - lo + 1)) - 1) << lo):
                raise U280Error(
                    f"slv1 流推不支持非连续 WSTRB: 0x{beat_mask:04x}")
            img = self._slv1_iq_img
            if img and self._slv1_iq_base != (addr & ~0xFFF):
                raise U280Error(
                    f"slv1 流推跨 subq 窗混杂: 已有 0x{self._slv1_iq_base:x} "
                    f"新 beat @0x{addr:08x}")
            if not img:
                self._slv1_iq_base = addr & ~0xFFF
            img.append((addr + lo, bytes(data[lo:hi + 1])))   # 保序追加
            return

        # 清除残留 staging/error
        self._bar_write32(PACKER_CTRL,
                          PACKER_CTRL_CLEAR_ERROR | PACKER_CTRL_CLEAR_STAGING)

        # 写入 4x32-bit staging data
        words = struct.unpack("<4I", data)
        for off, word in zip(PACKER_DATA, words):
            self._bar_write32(off, word)

        self._bar_write32(PACKER_WSTRB, beat_mask)
        self._bar_write32(PACKER_IQADDR, addr)

        # 检查 staging 完整
        status = self._bar_read32(PACKER_STATUS)
        if status & PACKER_STATUS_ANY_ERROR:
            raise U280Error(
                f"IQ packer pre-commit 错误: status=0x{status:08x}")
        stage_valid = (status >> PACKER_STATUS_STAGE_VALID_SHIFT) & 0x3F
        if stage_valid != 0x3F:
            raise U280Error(
                f"IQ packer staging 不完整: status=0x{status:08x} "
                f"stage_valid=0x{stage_valid:02x}")

        # Commit
        old_count = (status >> PACKER_STATUS_COMMIT_COUNT_SHIFT) & 0xFFFF
        self._bar_write32(PACKER_CTRL, PACKER_CTRL_COMMIT)
        self._packer_wait_commit(old_count)

    def _packer_wait_commit(self, old_count: int) -> None:
        """等待 packer commit 计数器 +1 且 pending 清零."""
        expected = (old_count + 1) & 0xFFFF
        deadline = time.time() + 1.0
        while time.time() < deadline:
            status = self._bar_read32(PACKER_STATUS)
            if status & PACKER_STATUS_ANY_ERROR:
                err_bits = []
                for bit, name in ((3, "incomplete"), (4, "unaligned"),
                                  (5, "bad_addr"), (6, "zero_wstrb")):
                    if status & (1 << bit):
                        err_bits.append(name)
                raise U280Error(
                    f"IQ packer 错误: status=0x{status:08x} "
                    f"({','.join(err_bits) or 'unknown'})")
            count = (status >> PACKER_STATUS_COMMIT_COUNT_SHIFT) & 0xFFFF
            if (count == expected
                    and not (status & PACKER_STATUS_PENDING)
                    and (status & PACKER_STATUS_READY)):
                return
            time.sleep(0.0001)
        raise U280Error(
            f"IQ packer commit 超时: old={old_count} expected={expected}")

    # ------------------------------------------------------------------
    # 执行 (stage 级)
    # ------------------------------------------------------------------

    def _slv1_flush_iq(self) -> None:
        """slv1: 按交付顺序把 beat 段逐笔推入 (队列追加语义).

        2026-11-01 两级定案:
        1. RTL (slv1_bada_rtl_verdict_20261101.md): 接收侧是固定槽位
           字节寻址 —— 指令必须落在 plane_base + 槽位固定偏移 (= 交付
           beat 的 ADDR 原值), 未用槽位不写, 无需 doorbell (压实连推
           是 BAD-A 根因);
        2. 板上实证: 同槽多指令 = 队列追加非重写 (last-write-wins 镜像
           只剩每槽末条指令, LUT 加载被 x 加载覆盖 -> BAD-B 零输出)。

        做法: beat 段严格按交付顺序, 仅合并前向相邻段 (同槽多指令地址
        回绕自然断段, 保证一事务一指令; RTL TEST3 证跨槽同事务无误),
        且**合并封顶 64B 使每 run 一笔 pwrite 发完, 切块循环永远真切不
        下去** (2026-11-01 RTL 附录 A TEST6: 从非 16B 对齐起点切 64B
        会把切块边界落在 16B 块中间 → 同一块被两笔事务续写 →
        dispatcher 双重垃圾 commit = BAD-C; 单条指令最长 22B < 64B,
        封顶永不断指令, "1422B 挂桥"约束也自然满足)。commit 触发规则
        (RTL 附录 A.6 勘误): 携槽尾块的拍即 commit —— 一事务可 commit
        多槽, 只写槽头的事务 commit 0 次; 追加/同址重写/多槽合并 burst
        不受限。xdma SGDMA write() 同步完成, 天然保序。
        """
        if self._slv1_preplan is not None:
            # 优化①: 预算好的 A.4 run 计划直推 (runs 均 <= _slv1_chunk,
            # 一 run 一笔 pwrite, 与逐 beat 路径的产出逐字节相同).
            _base, runs = self._slv1_preplan
            self._slv1_preplan = None
            assert self._h2c_fd is not None
            for off0, seg in runs:
                n = os.pwrite(self._h2c_fd, seg, SLV1_BASE + off0)
                if n != len(seg):
                    raise U280Error(
                        f"SGDMA IQ 流推失败 @0x{off0:08x}: "
                        f"{n}/{len(seg)} 字节")
            return
        img = self._slv1_iq_img
        if not img:
            return
        assert self._h2c_fd is not None
        runs: list[tuple[int, bytearray]] = []
        for start, payload in img:
            if (runs and start == runs[-1][0] + len(runs[-1][1])
                    and len(runs[-1][1]) + len(payload) <= self._slv1_chunk):
                runs[-1][1].extend(payload)
            else:
                runs.append((start, bytearray(payload)))
        for off0, seg in runs:
            pushed = 0
            while pushed < len(seg):
                chunk = bytes(seg[pushed:pushed + self._slv1_chunk])
                off = off0 + pushed
                n = os.pwrite(self._h2c_fd, chunk, SLV1_BASE + off)
                if n != len(chunk):
                    raise U280Error(
                        f"SGDMA IQ 流推失败 @0x{off:08x}: "
                        f"{n}/{len(chunk)} 字节")
                pushed += n
        self._slv1_iq_img = []

    def _start_and_wait(self, iq_mask: int, subq: int) -> None:
        """使能 IQ -> START -> 等 DONE -> 清中断.

        mock 后端: 仅记录到 _mock_start_log, 不动作.
        """
        if self.backend == "mock":
            self._mock_start_log.append((iq_mask, subq))
            return
        if self._slv1:
            self._slv1_flush_iq()   # stage 字节流先到位再 CFG/EN/START
        self._bar_write_checked(CSR_CFG, 0x1, "CFG")
        for idx, off in enumerate(CSR_IQ_EN):
            word = (iq_mask >> (32 * idx)) & 0xFFFFFFFF
            self._bar_write_checked(off, word, f"IQ_EN{idx}")
        self._bar_write32(CSR_START, ((subq & 0x3) << 8) | 0x1)
        if self.csr_settle_s > 0:
            time.sleep(self.csr_settle_s)
        self._wait_done()

    @staticmethod
    def _decode_int(int_word: int) -> str:
        """CSR_INT 字段解码: [0]=intr [1]=err [15:8]=hw_intid [23:16]=sw_intid."""
        return (f"intr={int_word & 0x1} err={(int_word >> 1) & 0x1} "
                f"hw_intid=0x{(int_word >> 8) & 0xFF:02x} "
                f"sw_intid=0x{(int_word >> 16) & 0xFF:02x}")

    def _abort_wedged_batch(self, why: str, last_int: int,
                            last_done: int) -> "U280Error":
        """批次卡死/出错后的收尾: 收集诊断 + 自动软复位, 返回待抛异常.

        死锁批次不会自行恢复 (SPEC §7.6.3 无超时), 不软复位则后续算子
        全部无法执行; 恢复失败时提示重新 init_device.
        """
        cfg = self._bar_read32(CSR_CFG)
        iq_en = [self._bar_read32(off) for off in CSR_IQ_EN]
        packer = 0 if self._slv1 else self._bar_read32(PACKER_STATUS)
        diag = (f"{why}: INT=0x{last_int:08x} ({self._decode_int(last_int)}) "
                f"DONE=0x{last_done:08x} CFG=0x{cfg:08x} "
                f"IQ_EN={[hex(w) for w in iq_en]} PACKER=0x{packer:08x}")
        if os.environ.get("LLAMA2_NO_AUTO_RESET"):
            # RTL 附录 B.4.2 现场保护模式: 不软复位 (软复位会清掉引擎侧
            # 证据, 尤其 R2 候选下 HBM/AXI 侧状态), 等 ILA/人工介入.
            diag += " | LLAMA2_NO_AUTO_RESET=1: 未软复位, 现场已保留"
            return U280Error(diag)
        try:
            self._reset_smi_hw()
            diag += " | 已自动软复位, 板卡恢复就绪, 可继续后续算子"
        except U280Error as exc:
            diag += f" | 自动软复位失败 ({exc}), 需重新 init_device()"
        return U280Error(diag)

    def _wait_done(self) -> None:
        """轮询 CSR_DONE 直到 [0]=1; err 位置位快速失败, 超时抛 U280Error.

        失败路径会先做 SMI 软复位再抛异常, 保证联调时一个卡死的算子
        不拖垮后续流程.
        """
        deadline = time.time() + self.timeout_s
        last_int, last_done = 0, 0
        while time.time() < deadline:
            last_int = self._bar_read32(CSR_INT)
            last_done = self._bar_read32(CSR_DONE)
            if last_done & 0x1:
                if last_int & 0x1:
                    self._ack_interrupt()
                return
            if last_int & 0x2:   # err: sticky 错误, 等下去也不会 DONE
                raise self._abort_wedged_batch(
                    "execute 硬件错误", last_int, last_done)
            time.sleep(self.poll_s)
        raise self._abort_wedged_batch(
            f"execute 超时 ({self.timeout_s}s)", last_int, last_done)

    def _ack_interrupt(self) -> None:
        """清除 DONE 中断."""
        int_word = self._bar_read32(CSR_INT)
        intid_hw = (int_word >> 8) & 0xFF
        int_word = (int_word & ~(0xFF << 16)) | (intid_hw << 16)
        self._bar_write32(CSR_INT, int_word)
        if self.csr_settle_s > 0:
            time.sleep(self.csr_settle_s)
        acked = self._bar_read32(CSR_INT)
        if acked & 0x1:
            raise U280Error(
                f"中断清除失败: before=0x{int_word:08x} after=0x{acked:08x}")

    # ------------------------------------------------------------------
    # 文件级 Launch IQ (sub_lcs 回归基线路径; 新算子请用 case 句柄制)
    # ------------------------------------------------------------------

    @staticmethod
    def iq_enable_mask(iq_files: Sequence) -> int:
        """由一组 IQFile 的 IQ_ID 推导 128-bit IQ_EN 掩码."""
        mask = 0
        for f in iq_files:
            if not (0 <= f.iq_id < 128):
                raise U280Error(f"IQ_ID 越界: {f.iq_id} (应为 0..127)")
            mask |= (1 << f.iq_id)
        return mask

    def load_iq_file(self, iq_file) -> int:
        """把一份 IQFile (或路径) 的全部 beat 经 packer 写入 IQ RAM.

        返回写入的 beat 数. 文件须先按需改写地址再装载.
        """
        self._check_ready()
        f = iq_file if isinstance(iq_file, IQFile) else IQFile.load(iq_file)
        n = 0
        for beat in f.beats():
            # DATA hex 高字节在前 -> 反转为 IQ RAM 小端字节序
            self._packer_write_beat(
                beat.addr, beat.mask, bytes.fromhex(beat.data_hex)[::-1])
            n += 1
        return n

    def launch_iq(self, iq_files: Sequence, subq: int = 0,
                  iq_mask: int | None = None,
                  allow_over_enable: bool = False) -> int:
        """装载一组 IQ 文件 -> 使能 -> START -> 阻塞等 DONE. 返回 beat 总数.

        iq_mask 缺省时由文件 IQ_ID 自动推导 (推荐). 显式传入的掩码默认
        不得包含**本批未装载**的通道: 那些通道窗口里残留着上一批的程序,
        使能后会重放旧装载与本批流量 CRD 错配, 硬件死锁 (2026-10-12 真机
        负面证据, test_u280_iqen_board.py); SPEC §11.2.5 语义 = 恰好使能
        本批装载的通道集合. 硬件探针确需超集掩码时显式传
        allow_over_enable=True 放行.
        """
        self._check_ready()
        files = [f if isinstance(f, IQFile) else IQFile.load(f)
                 for f in iq_files]
        if not files:
            raise U280Error("launch_iq 需要至少一个 IQ 文件")
        total = 0
        for f in files:
            total += self.load_iq_file(f)
        loaded_mask = self.iq_enable_mask(files)
        if iq_mask is None:
            mask = loaded_mask
        else:
            over = iq_mask & ~loaded_mask
            if over and not allow_over_enable:
                raise U280Error(
                    f"iq_mask 使能了本批未装载的通道: 多出 0x{over:x} "
                    f"(本批装载 0x{loaded_mask:x}). 残留程序重放会死锁硬件, "
                    f"确认在做硬件探测请传 allow_over_enable=True")
            mask = iq_mask
        self._start_and_wait(mask, subq)
        return total

    # ------------------------------------------------------------------
    # case 仓库: store / rebase / launch (算子主路径, 句柄制)
    # ------------------------------------------------------------------

    def _case_entry(self, handle: int) -> _CaseEntry:
        ent = self._case_registry.get(handle)
        if ent is None:
            raise U280Error(f"未知 case 句柄: {handle!r} (应先 store_case 入库)")
        return ent

    def store_case(self, case_dir, subq: int = 0) -> int:
        """把多 stage 算子 case 装入 Host DDR 仓库, 返回 int 句柄.

        流程: 发现 stage*/ 目录 (数字排序) -> 读每个 stage 的
        data_list_inst_subq{subq}.txt -> 按 data_list 顺序解析 IQ 文件
        存入仓库 (IQ 在仓库中的存储严格按 data_list 顺序); 若发现
        ldb_axi(27)/stb_axi(25) 通道, 逐条记下其指令存储的原始地址
        (AXIAddrRef), 供 rebase_case_axi 需要时更新 IQ 微指令中的地址.

        纯 Host 操作, 不要求 init_device. 返回的句柄是仓库原件的地址,
        原件自此只读 — 任何地址更新都经 rebase_case_axi 派生新句柄.
        """
        case = Path(case_dir).resolve()
        if not case.is_dir():
            raise U280Error(f"case 目录不存在: {case}")
        stages: list[_StageRecord] = []
        axi_refs: dict[int, list[AXIAddrRef]] = {IQ_ID_LDB_AXI: [],
                                                 IQ_ID_STB_AXI: []}
        for s_idx, stage_dir in enumerate(_discover_stage_dirs(case)):
            entries: list[_StageEntry] = []
            for e_idx, (mem_name, path) in enumerate(
                    _parse_data_list(stage_dir, subq)):
                f = IQFile.load(path)
                entries.append(_StageEntry(mem_name=mem_name, iq_id=f.iq_id,
                                           iqfile=f, beats=tuple(f.beats())))
                if f.iq_id in AXI_SLOT_IDS:
                    for i_idx, ins in enumerate(f.instructions):
                        axi_refs[f.iq_id].append(AXIAddrRef(
                            s_idx, e_idx, i_idx, ins.base_addr, ins.intlv_addr))
            # IQ_EN: vld 扫描推导 (SPEC §11.2.5/§8.8)
            mask = 0
            for e in entries:
                if any((ins.value >> INSTR_VLD_BIT) & 1
                       for ins in e.iqfile.instructions):
                    mask |= 1 << e.iq_id
            stages.append(_StageRecord(
                entries=tuple(entries),
                beats=tuple(b for e in entries for b in e.beats),
                iq_mask=mask))
        handle = self._next_case_handle
        self._next_case_handle += 1
        self._case_registry[handle] = _CaseEntry(
            case_dir=str(case), subq=subq, stages=tuple(stages),
            ldb_axi=tuple(axi_refs[IQ_ID_LDB_AXI]),
            stb_axi=tuple(axi_refs[IQ_ID_STB_AXI]))
        return handle

    def case_stages(self, handle: int) -> list[dict]:
        """句柄对应 case 的逐 stage 只读元信息 (调试/日志用)."""
        ent = self._case_entry(handle)
        out = []
        for i, st in enumerate(ent.stages):
            out.append({
                "stage": i,
                "mem_names": [e.mem_name for e in st.entries],
                "iq_ids": [e.iq_id for e in st.entries],
                "n_beats": len(st.beats),
                "iq_mask": st.iq_mask,
                "n_ldb_axi": sum(1 for r in ent.ldb_axi if r.stage_idx == i),
                "n_stb_axi": sum(1 for r in ent.stb_axi if r.stage_idx == i),
            })
        return out

    def rebase_case_axi(self, handle: int,
                        in_addrs: Sequence[int] | None = None,
                        out_addrs: Sequence[int] | None = None,
                        axi_rebase: dict[int, Sequence[int]] | None = None,
                        axi_intlv: dict[int, Sequence[int]] | None = None
                        ) -> int:
        """按需更新 IQ 微指令中的 HBM 地址, 返回**新句柄**; 原句柄不动.

        规则 (锚点平移, 保持生成器烘焙的流间相对布局):
        - LDB_AXI 全部指令: new_base = in_addrs[0] + (old_base − min_old_base)
        - STB_AXI 全部指令: new_base = out_addrs[0] + (old_base − min_old_base)
        - gr_intlv 默认不改写 (生成器烘焙的 SMC 交错步长).
        - axi_rebase={iq_id: [逐指令新 base]} 为显式逃生舱, 覆盖上述规则
          (多输入算子的指令→张量归属无 meta 时不可自动推导, 由调用方给定).
        - axi_intlv={iq_id: [逐指令新 intlv]} 同理显式改写交错步长
          (OEM 实证布局: 每流独立窗口 + intlv=per-SMC chunk, 见
          test_u280_sample_board.py; 与 axi_rebase 可叠加于同一指令).

        不传任何地址 => 无更新需求, 直接返回原句柄; case 无 AXI 通道同理.
        实现: 仅深拷贝含 AXI 指令的 entry, 位手术后重建其 beat 缓存;
        其余 stage/entry 与原句柄共享 (只读).
        """
        ent = self._case_entry(handle)
        if (in_addrs is None and out_addrs is None
                and axi_rebase is None and axi_intlv is None):
            return handle
        # (stage_idx, entry_idx) -> {inst_idx: [new_base|None, new_intlv|None]}
        patches: dict[tuple[int, int], dict[int, list[int | None]]] = {}

        def _put(ref: AXIAddrRef, base: int | None, intlv: int | None) -> None:
            for name, v in (("gr_base", base), ("gr_intlv", intlv)):
                if v is not None and not 0 <= v <= FIELD_MASK40:
                    raise U280Error(f"{name} 超出 40b: {v!r}")
            slot = patches.setdefault(
                (ref.stage_idx, ref.entry_idx), {}).setdefault(
                ref.inst_idx, [None, None])
            if base is not None:
                slot[0] = base & FIELD_MASK40
            if intlv is not None:
                slot[1] = intlv & FIELD_MASK40

        def _anchor_rebase(refs: tuple[AXIAddrRef, ...], anchor_new: int) -> None:
            anchor_old = min(r.old_base for r in refs)
            for r in refs:
                _put(r, anchor_new + (r.old_base - anchor_old), None)

        def _explicit(table: dict[int, Sequence[int]], which: str) -> None:
            for iq_id, vals in table.items():
                if iq_id == IQ_ID_LDB_AXI:
                    refs = ent.ldb_axi
                elif iq_id == IQ_ID_STB_AXI:
                    refs = ent.stb_axi
                else:
                    raise U280Error(
                        f"{which} 的 IQ_ID {iq_id} 不是 AXI 通道 "
                        f"({IQ_ID_STB_AXI}/{IQ_ID_LDB_AXI})")
                if len(vals) != len(refs):
                    raise U280Error(
                        f"{which}[{iq_id}] 给了 {len(vals)} 个值, "
                        f"该 case 实际有 {len(refs)} 条指令")
                for r, v in zip(refs, vals):
                    _put(r, v if which == "axi_rebase" else None,
                         v if which == "axi_intlv" else None)

        if in_addrs is not None and ent.ldb_axi:
            if not in_addrs:
                raise U280Error("in_addrs 为空: 至少需要一个输入基址")
            _anchor_rebase(ent.ldb_axi, in_addrs[0])
        if out_addrs is not None and ent.stb_axi:
            if not out_addrs:
                raise U280Error("out_addrs 为空: 至少需要一个输出基址")
            _anchor_rebase(ent.stb_axi, out_addrs[0])
        if axi_rebase:
            _explicit(axi_rebase, "axi_rebase")
        if axi_intlv:
            _explicit(axi_intlv, "axi_intlv")
        if not patches:
            return handle

        new_stages = list(ent.stages)
        by_stage: dict[int, dict[int, dict[int, list[int | None]]]] = {}
        for (s, e), inst_map in patches.items():
            by_stage.setdefault(s, {})[e] = inst_map
        for s, emap in by_stage.items():
            st = new_stages[s]
            new_entries = list(st.entries)
            for e, inst_map in emap.items():
                f = copy.deepcopy(new_entries[e].iqfile)
                for i_idx, (nb, ni) in inst_map.items():
                    _patch_axi_fields(f, f.instructions[i_idx], nb, ni)
                new_entries[e] = _StageEntry(new_entries[e].mem_name,
                                             f.iq_id, f, tuple(f.beats()))
            new_stages[s] = _StageRecord(
                entries=tuple(new_entries),
                beats=tuple(b for e in new_entries for b in e.beats),
                iq_mask=st.iq_mask)
        new_handle = self._next_case_handle
        self._next_case_handle += 1
        self._case_registry[new_handle] = _CaseEntry(
            case_dir=ent.case_dir, subq=ent.subq, stages=tuple(new_stages),
            ldb_axi=ent.ldb_axi, stb_axi=ent.stb_axi)
        return new_handle

    def launch_case(self, handle: int) -> list[int]:
        """逐 stage 装载并执行一个 case 句柄, 返回每 stage 的 beat 数.

        每 stage: beat 缓存按 data_list 顺序经 packer 灌入 IQ RAM ->
        IQ_EN (store 时 vld 扫描推导, 并校验 ⊆ 本 build 已实现通道)
        -> START -> 等 DONE -> 清中断, 然后进入下一 stage.
        同步语义: 返回即代表全部 stage DONE.
        """
        self._check_ready()
        ent = self._case_entry(handle)
        counts: list[int] = []
        for s_idx, st in enumerate(ent.stages):
            unsupported = st.iq_mask & ~self._iq_en_supported
            if unsupported:
                raise U280Error(
                    f"stage{s_idx} 使能了本 build 未实现的通道: "
                    f"掩码 0x{unsupported:x} (SPEC §11.2.1: 未实现通道的"
                    f"使能位只读 0, 批次将无法完成; 请与算子交付方核对)")
            for beat in st.beats:
                self._packer_write_beat(
                    beat.addr, beat.mask, bytes.fromhex(beat.data_hex)[::-1])
            try:
                self._start_and_wait(st.iq_mask, ent.subq)
            except U280Error as exc:
                raise U280Error(
                    f"launch_case handle={handle} stage{s_idx} "
                    f"({len(st.beats)} beats) 失败: {exc}") from exc
            counts.append(len(st.beats))
        return counts

    # ------------------------------------------------------------------
    # 地址改写 (R1): case 句柄制 rebase_case_axi (锚点平移) 为唯一入口;
    # 单 hex plane-image 路径 (execute/adjust_addr/_slot_rebase) 已于
    # 2026-10-14 废弃移除. sub_lcs 编译器 case 的文件级地址改写
    # (test_u280_board.py 的 patch_axi_file) 为回归基线, 保留在测试侧.
    # ------------------------------------------------------------------
