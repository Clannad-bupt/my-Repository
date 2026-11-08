"""
PhysicalMemoryLayout — AIACC 编译器统一物理地址仲裁者 (Phase 2)

[架构契约 Contract-1]
  整个编译流水线（compiler / optimizer / base / assembler）必须通过此类
  进行所有 logical_addr 的分配与拆分，禁止在其他模块中直接进行 bank×depth 运算。

设计原则：
  - SSoT: 唯一 bank_depth / bank_num 来源，始终从 HardwareSpec 读取
  - 冲突检测: validate_no_overlap() 在编译期暴露地址重叠
  - 不可变性: 已声明的 tensor 地址不可被 overwrite
"""

from typing import Dict, List, Optional, Tuple, Any
from dataclasses import dataclass, field


@dataclass
class AllocationRecord:
    name: str
    logical_addr: int
    role: str          # "input" | "output" | "constant" | "scratch"
    bank_id: int
    addr_in_bank: int


class PhysicalMemoryLayout:
    """
    统一地址仲裁者。
    
    使用方式：
        layout = PhysicalMemoryLayout.from_spec(hw_spec)
        layout.declare_tensor("x",   bank=0)          # 输入
        layout.declare_tensor("y",   bank=2)          # 输出
        layout.declare_tensor("w",   bank=7, offset=0)  # 常量
        scratch_addr = layout.allocate_scratch("inl_2")  # 中间变量
        bank, addr = layout.resolve(scratch_addr)
        layout.validate_no_overlap()                  # 编译期断言
    """

    def __init__(self, bank_depth: int, bank_num: int, scratch_bank: int):
        self._depth: int = bank_depth
        self._num: int   = bank_num
        self._scratch_bank: int = scratch_bank
        self._scratch_offset: int = 0

        # name → AllocationRecord
        self._allocations: Dict[str, AllocationRecord] = {}
        # logical_addr → name（用于冲突检测）
        self._addr_index: Dict[int, str] = {}

    # ------------------------------------------------------------------
    # 构造器
    # ------------------------------------------------------------------

    @classmethod
    def from_spec(cls, spec: Any, scratch_bank: Optional[int] = None) -> "PhysicalMemoryLayout":
        """
        从 HardwareSpec 构造，scratch_bank 自动选择第一个未被声明的 bank，
        或由调用方显式指定（用于多级流水线时的隔离）。
        """
        if not spec or not hasattr(spec, "params"):
            return cls(bank_depth=256, bank_num=8, scratch_bank=scratch_bank or 6)
        depth = getattr(spec.params, "PARAM_UR_BANK_DEPTH", 256)
        num   = getattr(spec.params, "PARAM_UR_BANK_NUM",   8)
        sb    = scratch_bank if scratch_bank is not None else (num - 2)  # 默认倒数第二个 bank
        return cls(bank_depth=depth, bank_num=num, scratch_bank=sb)

    # ------------------------------------------------------------------
    # 地址声明（已知 tensor）
    # ------------------------------------------------------------------

    def declare_tensor(self, name: str, bank: int, offset: int = 0,
                       role: str = "input") -> int:
        """
        登记一个已知地址的 tensor，返回 logical_addr。
        重复声明同名 tensor 会抛出错误（保护不可变性）。
        """
        if name in self._allocations:
            raise ValueError(
                f"[MemoryLayout] Tensor '{name}' already declared at "
                f"addr={self._allocations[name].logical_addr}. "
                f"Redeclaration forbidden."
            )
        logical = bank * self._depth + offset
        record = AllocationRecord(
            name=name, logical_addr=logical, role=role,
            bank_id=bank, addr_in_bank=offset
        )
        self._allocations[name] = record
        self._addr_index[logical] = name
        return logical

    def find_free_bank(self) -> int:
        """返回第一个未被 declare_tensor 占用的 bank。"""
        used = {r.bank_id for r in self._allocations.values()}
        for b in range(self._num):
            if b not in used:
                return b
        raise RuntimeError(
            f"[MemoryLayout] 所有 {self._num} 个 Bank 均已占用，无法分配新 bank。"
        )

    # ------------------------------------------------------------------
    # 地址分配（中间变量 / scratch）
    # ------------------------------------------------------------------

    def allocate_scratch(self, name: str) -> int:
        """
        为中间变量分配地址（在 scratch_bank 内递增）。
        同名中间变量只分配一次（Spill/Reload 共用同一地址）。
        """
        # 去掉 "spill_" / "reload_xxx_cN" 等前缀，找到原始生产者 key
        canonical = self._canonical_scratch_key(name)
        existing = self._find_scratch_by_key(canonical)
        if existing is not None:
            return existing

        logical = self._scratch_bank * self._depth + self._scratch_offset
        if self._scratch_offset >= self._depth:
            raise RuntimeError(
                f"[MemoryLayout] Scratch bank {self._scratch_bank} 已满 "
                f"({self._depth} 个 slot)，无法为 '{name}' 分配。"
            )
        # 🛸 [BUG-FIX] 将步长由 1 改为 16，防止多迭代（Loop Iterations）时的 stride 地址叠加冲突
        self._scratch_offset += 16

        record = AllocationRecord(
            name=f"_scratch_{canonical}", logical_addr=logical, role="scratch",
            bank_id=self._scratch_bank, addr_in_bank=logical % self._depth
        )
        self._allocations[f"_scratch_{canonical}"] = record
        self._addr_index[logical] = f"_scratch_{canonical}"
        return logical

    def _canonical_scratch_key(self, raw_name: str) -> str:
        """'spill_inl_2' / 'reload_inl_2_c0' → 'inl_2'"""
        key = raw_name.replace("spill_", "").replace("reload_", "")
        # 去掉 '_cN' 结尾（reload 编号）
        import re
        key = re.sub(r"_c\d+$", "", key)
        return key

    def _find_scratch_by_key(self, canonical: str) -> Optional[int]:
        rec = self._allocations.get(f"_scratch_{canonical}")
        return rec.logical_addr if rec else None

    # ------------------------------------------------------------------
    # 地址解析（统一拆分入口）
    # ------------------------------------------------------------------

    def resolve(self, logical_addr: int) -> Tuple[int, int]:
        """
        唯一的 BANK / ADDR 拆分点。
        Returns: (bank_id, addr_in_bank)
        """
        return logical_addr // self._depth, logical_addr % self._depth

    # ------------------------------------------------------------------
    # 编译期冲突检测
    # ------------------------------------------------------------------

    def validate_no_overlap(self) -> List[str]:
        """
        检查所有已分配的地址是否有重叠。
        Returns: 冲突描述列表（空 = 无冲突）。
        """
        seen: Dict[int, str] = {}
        conflicts = []
        for name, rec in self._allocations.items():
            addr = rec.logical_addr
            if addr in seen:
                conflicts.append(
                    f"CONFLICT: '{name}' (bank={rec.bank_id}, addr={rec.addr_in_bank}) "
                    f"与 '{seen[addr]}' 冲突于 logical_addr={addr}"
                )
            else:
                seen[addr] = name
        return conflicts

    # ------------------------------------------------------------------
    # 属性访问
    # ------------------------------------------------------------------

    @property
    def bank_depth(self) -> int:
        return self._depth

    @property
    def bank_num(self) -> int:
        return self._num

    @property
    def scratch_bank(self) -> int:
        return self._scratch_bank

    def get_logical_addr(self, name: str) -> Optional[int]:
        rec = self._allocations.get(name)
        return rec.logical_addr if rec else None

    def summary(self) -> str:
        lines = [f"PhysicalMemoryLayout (depth={self._depth}, num={self._num}, scratch_bank={self._scratch_bank})"]
        for name, rec in sorted(self._allocations.items(), key=lambda x: x[1].logical_addr):
            lines.append(f"  {name:<30s} → bank={rec.bank_id:2d} addr={rec.addr_in_bank:3d} "
                         f"(logical={rec.logical_addr:4d}) [{rec.role}]")
        return "\n".join(lines)
