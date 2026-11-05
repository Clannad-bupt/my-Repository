"""IQ 映射表: 算子输入/输出 shape、dtype 与 IQ 文件路径的单点注册表.

定位:
- 各算子负责人将实现好的 IQ 文件放入 ``IQ/`` 目录, 本表登记每个算子的
  输入/输出 shape、dtype 与 IQ 文件路径。
- shape/dtype 逐行对齐算子需求表 §2/§3/§4/§5;
  ``case`` 命名为 golden 用例名去掉 phase 前缀 (用例名 = ``{phase}_{case}``),
  使 需求表 → numpy 参考 (``test/operators.py``) → golden 用例
  (``cases/{phase}_{case}.npz``) → IQ 文件 全链路可追踪。
  两处刻意例外: ``matmul_xv`` ↔ golden ``*_matmul_av``,
  ``add_f16`` ↔ golden ``*_add_residual`` (见 test/test_iq_board.py 的 RECIPES)。

约定:
- IQ 文件放入 ``IQ/`` 目录; 交付后由负责人在对应条目的 ``iqfile``
  字段填写相对仓库根目录的路径 (默认 ``""`` 表示未交付)。
  hex 格式遵循仓库约定: 每行一个 16B, 高字节在前; 装载时逐行反转为硬件小端
  (解析不强制行宽, 仅校验 hex 合法性)。
- ``load_all_iq()`` 把所有已交付 iqfile 解析装载到对应条目的 ``iq`` 成员
  (bytes, 逐行小端, 即可直接提交 IQP1 的 IQ 序列)。
- 常量固化原则 (ADR-0002): 相同 shape 的多次调用共用同一份 IQ
  (如 QKVO×4、gate/up×2、残差 Add×2)。
- 每次 execute 提交的 IQ 包 ≤1024 条 128-bit IQ (IQP1 上限)。

UNVERIFIED: R1 — 本表假设同一份 IQ 可在装载时改写 HBM 基址后多次复用
  (以及 decode 每步对 K/V cache 槽位地址的改写均依赖 R1)。
  R1 spike 通过前, 一切复用均为假设。
UNVERIFIED: R2 — Gather 的 indices 假设为运行时 HBM 数据。
UNVERIFIED: R3 — rsqrt 的 in1 magic LUT ((128,) U8) 布局为按 OEM 64B 交付
  内容的外推, 待与算子 owner 确认 (见 README §未验证假设 R3)。
其余悬空项: uint4 物理 HBM 布局 (Q-W4)、8-SMC 分片方式 (Q-PAR)、
RMSNorm/RoPE 分解完整性 (Q-RMS/Q-ROPE)。
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# 模型常量 (Llama2-7B; ADR-0002 固定 256 槽 KV cache)
# ---------------------------------------------------------------------------

HIDDEN = 4096
FFN = 11008
HEADS = 32
HEAD_DIM = 128
SEQ = 256            # KV cache 槽位数; prompt+generate 合计上限
VOCAB = 32000
GROUP = 128          # AWQ group_size
EPS = 1.0e-5

# rsqrt in1 的固定内容 (OEM 权重, 128B): 4 个 f16 常数 (小端)
# 0x3e00/0x3800/0x0001/0x59bb, 每个广播 16 个 lane (64×f16 = 128B),
# 与 rsqrt 登记 in1 (128,) U8 一致。上板 (llama2.py) 与测试 golden 共用此
# 常量, 单点维护。UNVERIFIED: R3 — OEM 交付内容只有每个常数 8 lane (64B),
# 128B 完整表布局是按同一模式外推的, 待与算子 owner (刘磊) 确认
# (README §未验证假设 R3)。
RSQRT_MAGIC_LUT: bytes = b"".join(
    w.to_bytes(2, "little") * 16 for w in (0x3E00, 0x3800, 0x0001, 0x59BB))

# dtype 标签; uint4 以 uint8 数组承载, 打包率 2 元素/字节 (逻辑视图)
F16, F32, U4, I32, U16, U8 = "f16", "f32", "uint4", "int32", "uint16", "uint8"
_DTYPE_BYTES = {F16: 2.0, F32: 4.0, U4: 0.5, I32: 4.0, U16: 2.0, U8: 1.0}

REPO_ROOT = Path(__file__).resolve().parent

# ---------------------------------------------------------------------------
# 数据结构
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class TensorSpec:
    """一个输入/输出张量的逻辑视图 (slot 对应需求表 §1 的 in0/in1/.../out0 槽位)."""

    slot: str                # 'in0'... / 'out0'
    shape: tuple[int, ...]
    dtype: str               # F16/F32/U4/I32

    @property
    def numel(self) -> int:
        return math.prod(self.shape)

    @property
    def nbytes(self) -> int:
        return math.ceil(self.numel * _DTYPE_BYTES[self.dtype])


@dataclass(frozen=True)
class IQSpec:
    """一条算子登记项: 需求表一行 (或 Q-RMS/Q-ROPE 登记的缺行) 对应一份 IQ 文件.

    (phase, case) 是唯一键; phase 单列字段, 不再由所属分组推导,
    但自检会校验它与所在分组 (_PREFILL/_DECODE/_COMMON) 一致。

    iqfile 是路径 (负责人交付时填写), iq 是装载后的内容 (load_iq()/
    load_all_iq() 填充), 二者分开: 表本身保持可 pickle/可比对,
    内容按需装载。
    """

    case: str                            # 算子名, 不带 phase 前缀 (同 phase 内唯一)
    phase: str                           # 'prefill' | 'decode' | 'common'
    owner: str                           # RTL 负责人 (需求表/STATUS 签收矩阵)
    inputs: tuple[TensorSpec, ...]
    outputs: tuple[TensorSpec, ...]
    iqfile: str = ""                     # IQ 文件路径 (相对仓库根目录);
    # 交付后由负责人填写, "" = 未交付
    iq: bytes | None = field(default=None, repr=False, compare=False)
    # iqfile 的内容: 逐行解析并反转后的硬件小端 IQ 序列; None = 未装载


def _t(slot: str, shape: tuple, dtype: str) -> TensorSpec:
    return TensorSpec(slot, shape, dtype)


# ---------------------------------------------------------------------------
# Prefill (需求表 §2; RMSNorm/RoPE 原始算子见 §5)
# ---------------------------------------------------------------------------

_PREFILL: tuple[IQSpec, ...] = (
    # sample，仅用于示范
    IQSpec(
        case="sample",
        phase="prefill",
        owner="示例负责人",
        inputs=(_t("in0", (1, SEQ, HIDDEN), F16),),
        outputs=(_t("out0", (1, SEQ, HIDDEN), F16),),
        iqfile="IQ/sample.hex",
    ),
    # ---- RMSNorm 分解 (§5): ReduceMean(x²)+eps → Rsqrt → Mul → Mul(weight) ----
    IQSpec(
        # ReduceMean(x²) + eps
        case="reduce_mean_square",
        phase="prefill",
        owner="冯海峻",
        inputs=(_t("in0", (1, SEQ, HIDDEN), F16),),
        outputs=(_t("out0", (1, SEQ, 1), F16),),
    ),
    IQSpec(
        case="rsqrt", phase="prefill", owner="刘磊",
        inputs=(
            _t("in0", (1, SEQ, 1), F16),
            _t("in1", (128,), U8),   # rsqrt magic LUT (OEM 权重)
        ),
        outputs=(_t("out0", (1, SEQ, 1), F16),),
    ),
    IQSpec(
        case="rmsnorm_mul_rsqrt", phase="prefill", owner="何佳宝",
        inputs=(
            _t("in0", (1, SEQ, HIDDEN), F16),
            _t("in1", (1, SEQ, 1), F16),
        ),
        outputs=(_t("out0", (1, SEQ, HIDDEN), F16),),
    ),
    IQSpec(
        case="rmsnorm_mul_weight", phase="prefill", owner="何佳宝",
        inputs=(
            _t("in0", (1, SEQ, HIDDEN), F16),
            _t("in1", (1, 1, HIDDEN), F16),
        ),
        outputs=(_t("out0", (1, SEQ, HIDDEN), F16),),
    ),
    # ---- QKVO 投影 ----
    IQSpec(
        case="matmul_w4_qkvo",
        phase="prefill",
        owner="张文扬",
        inputs=(
            _t("in0", (1, SEQ, HIDDEN), F16),
            _t("in1", (HIDDEN, HIDDEN), U4),
            _t("in2", (HIDDEN, HIDDEN//GROUP), F16),
            _t("in3", (HIDDEN, HIDDEN//GROUP), U4),
        ),
        outputs=(_t("out0", (1, SEQ, HIDDEN), F16),),
    ),
    # ---- RoPE: cos/sin Gather + 分解算子 (§5) ----
    IQSpec(
        case="rotate_half", phase="prefill", owner="李思颖",
        inputs=(_t("in0", (1, SEQ, HEADS, HEAD_DIM), F16),),
        outputs=(_t("out0", (1, SEQ, HEADS, HEAD_DIM), F16),),
    ),
    IQSpec(
        case="rope_mul_cos", phase="prefill", owner="何佳宝",
        inputs=(
            _t("in0", (1, SEQ, HEADS, HEAD_DIM), F16),
            _t("in1", (1, SEQ, 1, HEAD_DIM), F16),
        ),
        outputs=(_t("out0", (1, SEQ, HEADS, HEAD_DIM), F16),),
    ),
    # ---- Attention: QK^T → maskAdd → Softmax → ×V (执行序, 非表内行序) ----
    IQSpec(
        case="matmul_qk", phase="prefill", owner="王俊驰",
        inputs=(
            _t("in0", (1, SEQ, HEADS, HEAD_DIM), F16),
            _t("in1", (1, SEQ, HEADS, HEAD_DIM), F16),
        ),
        outputs=(_t("out0", (1, HEADS, SEQ, SEQ), F16),),
    ),
    IQSpec(
        case="add_mask", phase="prefill", owner="孙浩南",
        inputs=(
            _t("in0", (1, HEADS, SEQ, SEQ), F16),
            _t("in1", (1, 1, SEQ, SEQ), F16),
        ),
        outputs=(_t("out0", (1, HEADS, SEQ, SEQ), F16),),
    ),
    IQSpec(
        case="softmax", phase="prefill", owner="杨凯程",
        inputs=(_t("in0", (1, HEADS, SEQ, SEQ), F16),),
        outputs=(_t("out0", (1, HEADS, SEQ, SEQ), F16),),
    ),
    IQSpec(
        case="matmul_xv", phase="prefill", owner="王俊驰",
        inputs=(
            _t("in0", (1, HEADS, SEQ, SEQ), F16),
            _t("in1", (1, SEQ, HEADS, HEAD_DIM), F16),
        ),
        outputs=(_t("out0", (1, SEQ, HEADS, HEAD_DIM), F16),),
    ),
    # ---- 残差 ----
    IQSpec(
        case="add_f16", phase="prefill", owner="孙浩南",
        inputs=(
            _t("in0", (1, SEQ, HIDDEN), F16),
            _t("in1", (1, SEQ, HIDDEN), F16),
        ),
        outputs=(_t("out0", (1, SEQ, HIDDEN), F16),),
    ),
    # ---- FFN: gate/up → SiLU → Mul → down ----
    IQSpec(
        case="matmul_w4_gate_up",
        phase="prefill",
        owner="张文扬",
        inputs=(
            _t("in0", (1, SEQ, HIDDEN), F16),
            _t("in1", (FFN, HIDDEN), U4),
            _t("in2", (FFN, HIDDEN//GROUP), F16),
            _t("in3", (FFN, HIDDEN//GROUP), U4),
        ),
        outputs=(_t("out0", (1, SEQ, FFN), F16),),
    ),
    IQSpec(
        case="silu", phase="prefill", owner="杨凯程",
        inputs=(_t("in0", (1, SEQ, FFN), F16),),
        outputs=(_t("out0", (1, SEQ, FFN), F16),),
    ),
    IQSpec(
        case="mul_gate", phase="prefill", owner="何佳宝",
        inputs=(
            _t("in0", (1, SEQ, FFN), F16),
            _t("in1", (1, SEQ, FFN), F16),
        ),
        outputs=(_t("out0", (1, SEQ, FFN), F16),),
    ),
    IQSpec(
        case="matmul_w4_down",
        phase="prefill",
        owner="张文扬",
        inputs=(
            _t("in0", (1, SEQ, FFN), F16),
            _t("in1", (HIDDEN, FFN), U4),
            _t("in2", (HIDDEN, FFN//GROUP), F16),
            _t("in3", (HIDDEN, FFN//GROUP), U4),
        ),
        outputs=(_t("out0", (1, SEQ, HIDDEN), F16),),
    ),
)

# ---------------------------------------------------------------------------
# Decode (需求表 §3): 与 prefill 同构, 序列维 256 → 1; K/V 侧保持 256 槽
# ---------------------------------------------------------------------------

_DECODE: tuple[IQSpec, ...] = (
    IQSpec(
        # ReduceMean(x²) + eps
        case="reduce_mean_square",
        phase="decode",
        owner="冯海峻",
        inputs=(_t("in0", (1, 1, HIDDEN), F16),),
        outputs=(_t("out0", (1, 1, 1), F16),),
    ),
    IQSpec(
        case="rsqrt", phase="decode", owner="刘磊",
        inputs=(
            _t("in0", (1, 1, 1), F16),
            _t("in1", (128,), U8),   # rsqrt magic LUT (OEM 权重)
        ),
        outputs=(_t("out0", (1, 1, 1), F16),),
    ),
    IQSpec(
        case="rmsnorm_mul_rsqrt", phase="decode", owner="何佳宝",
        inputs=(
            _t("in0", (1, 1, HIDDEN), F16),
            _t("in1", (1, 1, 1), F16),
        ),
        outputs=(_t("out0", (1, 1, HIDDEN), F16),),
    ),
    IQSpec(
        case="rmsnorm_mul_weight", phase="decode", owner="何佳宝",
        inputs=(
            _t("in0", (1, 1, HIDDEN), F16),
            _t("in1", (1, 1, HIDDEN), F16),
        ),
        outputs=(_t("out0", (1, 1, HIDDEN), F16),),
    ),
    IQSpec(
        case="matmul_w4_qkvo", phase="decode", owner="张文扬",
        inputs=(
            _t("in0", (1, 1, HIDDEN), F16),
            _t("in1", (HIDDEN, HIDDEN), U4),
            _t("in2", (HIDDEN, HIDDEN//GROUP), F16),
            _t("in3", (HIDDEN, HIDDEN//GROUP), U4),
        ),
        outputs=(_t("out0", (1, 1, HIDDEN), F16),),
    ),
    IQSpec(
        case="rotate_half", phase="decode", owner="李思颖",
        inputs=(_t("in0", (1, 1, HEADS, HEAD_DIM), F16),),
        outputs=(_t("out0", (1, 1, HEADS, HEAD_DIM), F16),),
    ),
    IQSpec(
        case="rope_mul_cos", phase="decode", owner="何佳宝",
        inputs=(
            _t("in0", (1, 1, HEADS, HEAD_DIM), F16),
            _t("in1", (1, 1, 1, HEAD_DIM), F16),
        ),
        outputs=(_t("out0", (1, 1, HEADS, HEAD_DIM), F16),),
    ),
    IQSpec(
        case="matmul_qk", phase="decode", owner="王俊驰",
        inputs=(
            _t("in0", (1, 1, HEADS, HEAD_DIM), F16),
            _t("in1", (1, SEQ, HEADS, HEAD_DIM), F16),
        ),
        outputs=(_t("out0", (1, HEADS, 1, SEQ), F16),),
    ),
    IQSpec(
        case="add_mask", phase="decode", owner="孙浩南",
        inputs=(
            _t("in0", (1, HEADS, 1, SEQ), F16),
            _t("in1", (1, 1, 1, SEQ), F16),
        ),
        outputs=(_t("out0", (1, HEADS, 1, SEQ), F16),),
    ),
    IQSpec(
        case="softmax", phase="decode", owner="杨凯程",
        inputs=(_t("in0", (1, HEADS, 1, SEQ), F16),),
        outputs=(_t("out0", (1, HEADS, 1, SEQ), F16),),
    ),
    IQSpec(
        case="matmul_xv", phase="decode", owner="王俊驰",
        inputs=(
            _t("in0", (1, HEADS, 1, SEQ), F16),
            _t("in1", (1, SEQ, HEADS, HEAD_DIM), F16),
        ),
        outputs=(_t("out0", (1, 1, HEADS, HEAD_DIM), F16),),
    ),
    IQSpec(
        case="add_f16", phase="decode", owner="孙浩南",
        inputs=(
            _t("in0", (1, 1, HIDDEN), F16),
            _t("in1", (1, 1, HIDDEN), F16),
        ),
        outputs=(_t("out0", (1, 1, HIDDEN), F16),),
    ),
    IQSpec(
        case="matmul_w4_gate_up", phase="decode", owner="张文扬",
        inputs=(
            _t("in0", (1, 1, HIDDEN), F16),
            _t("in1", (FFN, HIDDEN), U4),
            _t("in2", (FFN, HIDDEN//GROUP), F16),
            _t("in3", (FFN, HIDDEN//GROUP), U4),
        ),
        outputs=(_t("out0", (1, 1, FFN), F16),),
    ),
    IQSpec(
        case="silu", phase="decode", owner="杨凯程",
        inputs=(_t("in0", (1, 1, FFN), F16),),
        outputs=(_t("out0", (1, 1, FFN), F16),),
    ),
    IQSpec(
        case="mul_gate", phase="decode", owner="何佳宝",
        inputs=(
            _t("in0", (1, 1, FFN), F16),
            _t("in1", (1, 1, FFN), F16),
        ),
        outputs=(_t("out0", (1, 1, FFN), F16),),
    ),
    IQSpec(
        case="matmul_w4_down", phase="decode", owner="张文扬",
        inputs=(
            _t("in0", (1, 1, FFN), F16),
            _t("in1", (HIDDEN, FFN), U4),
            _t("in2", (HIDDEN, FFN//GROUP), F16),
            _t("in3", (HIDDEN, FFN//GROUP), U4),
        ),
        outputs=(_t("out0", (1, 1, HIDDEN), F16),),
    ),
)

# ---------------------------------------------------------------------------
# LM head (需求表 §4, ADR-0003)
# ---------------------------------------------------------------------------

_COMMON: tuple[IQSpec, ...] = (
    IQSpec(
        case="lmhead_matmul_nt_f16",
        phase="common",
        owner="何佳宝",
        inputs=(
            _t("in0", (1, HIDDEN), F16),
            _t("in1", (VOCAB, HIDDEN), F16),
        ),
        outputs=(_t("out0", (1, VOCAB), F32),),
    ),
)

# ---------------------------------------------------------------------------
# 注册表与查询
# ---------------------------------------------------------------------------

IQ_MAP: tuple[IQSpec, ...] = _PREFILL + _DECODE + _COMMON

# 分组只决定表内排版/顺序; phase 的真值在条目字段上, 两者一致性由
# check_consistency() 校验 (防止插错分组或写错字段)。
_PHASES = ("prefill", "decode", "common")
_GROUPS: dict[str, tuple[IQSpec, ...]] = dict(
    zip(_PHASES, (_PREFILL, _DECODE, _COMMON)))


def iq_file(spec: IQSpec) -> Path | None:
    """登记项对应的 IQ 文件绝对路径; iqfile 未填写时返回 None."""
    return REPO_ROOT / spec.iqfile if spec.iqfile else None


def _label(spec: IQSpec) -> str:
    """报错用标签 ``phase/case``."""
    return f"{spec.phase}/{spec.case}"


def parse_iq_hex(text: str) -> bytes:
    """解析 IQ hex 文本 → 硬件小端 IQ 序列.

    仓库约定: 每行一个 16B, 高字节在前; 装载时逐行反转为小端后拼接.
    解析只校验 hex 合法性, 不强制行宽 (sample.hex 等既有文件为 14B/行).
    """
    rows: list[bytes] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            row = bytes.fromhex(line)
        except ValueError as exc:
            raise ValueError(f"第 {lineno} 行不是合法 hex: {line!r}") from exc
        rows.append(row[::-1])
    return b"".join(rows)


def load_iq(spec: IQSpec) -> bytes:
    """读取 spec.iqfile, 解析为硬件小端 IQ 序列并写入 spec.iq, 返回内容.

    iqfile 未填写或文件不存在时抛 FileNotFoundError。
    """
    path = iq_file(spec)
    if path is None:
        raise FileNotFoundError(f"{_label(spec)}: iqfile 未填写")
    if not path.is_file():
        raise FileNotFoundError(f"{_label(spec)}: {path} 不存在")
    try:
        data = parse_iq_hex(path.read_text())
    except ValueError as exc:
        raise ValueError(f"{_label(spec)}: {path.name}: {exc}") from exc
    object.__setattr__(spec, "iq", data)   # frozen dataclass, 内容为装载态
    return data


def load_all_iq() -> list[IQSpec]:
    """装载全部已交付的 iqfile 到各自 spec.iq; 返回成功装载的登记项列表.

    iqfile 未填写的条目跳过 (视作未交付); 已填写但文件缺失或格式错误时
    抛异常 — 交付状态不一致应当立即暴露, 不静默跳过。
    """
    loaded: list[IQSpec] = []
    for spec in IQ_MAP:
        if spec.iqfile:
            load_iq(spec)
            loaded.append(spec)
    return loaded


_BY_KEY: dict[tuple[str, str], IQSpec] = {
    (s.phase, s.case): s for s in IQ_MAP}


def by_case(case: str, phase: str) -> IQSpec:
    """按 (phase, case) 查登记项; case 不带 phase 前缀, 与 phase 组合才唯一."""
    return _BY_KEY[(phase, case)]


def by_phase(phase: str) -> list[IQSpec]:
    """按阶段过滤: 'prefill' | 'decode' | 'common'."""
    return [s for s in IQ_MAP if s.phase == phase]


def by_owner(owner: str) -> list[IQSpec]:
    """按负责人过滤 (支持子串匹配, 如 by_owner('张文扬'))."""
    return [s for s in IQ_MAP if owner in s.owner]


def missing_iq_files() -> list[str]:
    """交付进度自检: iqfile 字段未填写, 或所填路径的文件不存在."""
    missing: list[str] = []
    for s in IQ_MAP:
        if not s.iqfile:
            missing.append(f"{_label(s)} (iqfile 未填写)")
        elif not iq_file(s).is_file():
            missing.append(s.iqfile)
    return missing


def check_consistency() -> list[str]:
    """结构自检; 返回发现的问题列表 (空 = 通过)."""
    problems: list[str] = []
    for s in IQ_MAP:
        if s.phase not in _GROUPS:
            problems.append(f"{s.case}: 未知 phase {s.phase!r}")
    for phase, group in _GROUPS.items():
        for s in group:
            if s.phase != phase:
                problems.append(
                    f"{s.case}: phase={s.phase!r} 与所在分组 {phase!r} 不一致")
    if len(_BY_KEY) != len(IQ_MAP):
        seen, dup = set(), set()
        for s in IQ_MAP:
            key = (s.phase, s.case)
            (dup if key in seen else seen).add(key)
        problems.append(f"(phase, case) 名重复: {sorted(dup)}")
    for s in IQ_MAP:
        for t in (*s.inputs, *s.outputs):
            if t.dtype not in _DTYPE_BYTES:
                problems.append(f"{s.case}.{t.slot}: 未知 dtype {t.dtype!r}")
    seen_iq: dict[str, str] = {}
    for s in IQ_MAP:
        if s.iqfile and s.iqfile in seen_iq:
            problems.append(
                f"iqfile 路径重复: {s.iqfile} ({seen_iq[s.iqfile]} 与 {s.case})")
        elif s.iqfile:
            seen_iq[s.iqfile] = s.case
    return problems


def _fmt_tensor(t: TensorSpec) -> str:
    return f"{t.slot} {'x'.join(map(str, t.shape))}x{t.dtype}"


def dump_table() -> None:
    """打印全量登记项与 IQ 文件存在性 — 里程碑 M1 的交付清单视图."""
    problems = check_consistency()
    print(f"{'phase':8} {'case':24} {'owner':10} iqfile")
    print("-" * 100)
    for s in IQ_MAP:
        f = iq_file(s)
        if not s.iqfile:
            shown, flag = "-", "未填写"
        elif f is not None and f.is_file():
            shown, flag = s.iqfile, "OK"
        else:
            shown, flag = s.iqfile, "缺失"
        print(f"{s.phase:8} {s.case:24} {s.owner:10} {shown} [{flag}]")
        for t in (*s.inputs, *s.outputs):
            print(f"    {_fmt_tensor(t)}")
    missing = missing_iq_files()
    print("-" * 100)
    print(f"共 {len(IQ_MAP)} 条登记项, IQ 文件已交付 "
          f"{len(IQ_MAP) - len(missing)}, 缺失 {len(missing)}")
    if problems:
        print("结构自检发现问题:")
        for p in problems:
            print(f"  ! {p}")
    else:
        print("结构自检通过")


if __name__ == "__main__":
    dump_table()
