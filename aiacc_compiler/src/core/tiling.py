"""
AIACC Tiling Pass (V1.0)
========================
职责：给定输入张量形状 N 和硬件向量宽度，计算：
  - K: 主循环次数 (完整向量批次)
  - r: 尾部余数 (0-3 个元素)
  - address_stride: Bank 地址步进（行粒度）

这是 PassManager 5.3 的实现，下游由 LoopStitcher 消费。
"""
from typing import Tuple
from dataclasses import dataclass


@dataclass
class TilingResult:
    """分块结果描述符。"""
    N: int             # 总元素数
    vec_width: int     # 向量宽度（硬件 lane 数，通常为 4）
    K: int             # 主循环次数 = N // vec_width
    r: int             # 尾部余数 = N % vec_width  (0 ~ vec_width-1)
    address_stride: int  # Bank 地址步进（每次迭代地址 +stride 行）

    def has_tail(self) -> bool:
        return self.r > 0

    def __repr__(self):
        tail = f", tail={self.r}" if self.has_tail() else ""
        return (f"TilingResult(N={self.N}, vec={self.vec_width}, "
                f"K={self.K}{tail}, stride={self.address_stride})")


class TilingPass:
    """
    AIACC 一维分块策略（V1.0）。
    当前支持：
      - 1-D 向量分块（最内层）
      - 固定 vec_width（由硬件决定，默认 4 × f32 = 128-bit）
      - 编译期静态 N（AOT 模式）
    后续扩展：
      - 2-D / N-D 分块（Tiled-Loop + Compactor 7层嵌套）
      - 运行时动态 N（需硬件 LOOPS 寄存器间接寻址）
    """

    def __init__(self, vec_width: int = 4):
        """
        Args:
            vec_width: 硬件每次处理的元素数（1 Bank Row = vec_width × f32）。
                       AOS 3.5 标准：128-bit / 4 bytes = 4 lanes。
        """
        if vec_width <= 0:
            raise ValueError(f"vec_width 必须为正整数，got {vec_width}")
        self.vec_width = vec_width

    def tile(self, N: int, address_stride: int = 1) -> TilingResult:
        """
        对长度为 N 的一维输入进行分块。

        Args:
            N: 输入元素总数（编译期已知）。
            address_stride: 每次迭代 Bank 地址偏移的行数（默认 1 行 = 4 元素）。

        Returns:
            TilingResult

        Raises:
            ValueError: N <= 0
        """
        if N <= 0:
            raise ValueError(f"N 必须为正整数，got {N}")

        K = N // self.vec_width
        r = N % self.vec_width

        result = TilingResult(
            N=N,
            vec_width=self.vec_width,
            K=K,
            r=r,
            address_stride=address_stride,
        )

        print(f"[TILING] N={N}, vec_width={self.vec_width} → {result}")
        return result
