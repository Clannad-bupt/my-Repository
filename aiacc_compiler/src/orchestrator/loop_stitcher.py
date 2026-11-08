"""
AIACC Loop Stitcher (V1.0)
==========================
职责：将 (DFG 模板, TilingResult, II) 展开为带 loop/inc 字段的物理指令流。

架构位置：在 HSTSStitcher 之上，Emitter 之前。

生成策略：
  1. 主循环（K 次）：
     - 在 UR_READ(operand_a) 注入 outer_loops=K-1, outer_inc=stride
     - 在 UR_WRITE(output)   注入 outer_loops=K-1, outer_inc=stride
     - Compactor 的 _pass1_linear_folding 识别 outer_loops 生成 LOOP_HDR
  2. 尾部（r > 0）：
     - 复制 DFG 模板 r 次（标量展开，每次取 1 元素）
     - 地址偏移 = K * stride（接着主循环）
     - 不带 loop 字段，直接发射
"""
import copy
from typing import List, Dict, Optional, Tuple
from ir.mir.mir_op import MIROp
from orchestrator.stitcher import HSTSStitcher
from core.tiling import TilingResult


class LoopStitcher:
    """
    AIACC 循环感知缝合器。

    用法示例：
        ls = LoopStitcher(hw_spec)
        stream = ls.stitch_vectorized(
            op_name="sin",
            dfg_template=get_sin_standard_dfg(),
            synthesis_res=smt_result["schedule"],
            II=smt_result["II"],
            tiling=TilingPass(4).tile(N=40),
        )
    """

    def __init__(self, hw_spec=None):
        self.hw_spec = hw_spec
        self.base_stitcher = HSTSStitcher()

    # ──────────────────────────────────────────────────────────────────
    #  Public API
    # ──────────────────────────────────────────────────────────────────

    def stitch_vectorized(
        self,
        op_name: str,
        dfg_template: List[MIROp],
        synthesis_res: Dict[str, int],
        II: int,
        tiling: TilingResult,
    ) -> List[MIROp]:
        """
        主入口：生成自适应循环指令流。

        Args:
            op_name:        算子名称（用于日志）
            dfg_template:   SMT 已分配物理资源的 DFG（单次迭代）
            synthesis_res:  {trace_id → local_t} SMT 调度结果
            II:             IIAnalyzer 给出的理论 II（cycles）
            tiling:         TilingPass.tile() 的结果

        Returns:
            List[MIROp]: 完整的全局物理指令流（含 loop/inc 注解）
        """
        K, r, stride = tiling.K, tiling.r, tiling.address_stride

        print(f"\n[LOOP-STITCH] {op_name}: K={K}, r={r}, II={II}, stride={stride}")

        final_stream: List[MIROp] = []

        # ── Case 1: K >= 1，生成主循环 ──────────────────────────────
        if K >= 1:
            main_stream = self._gen_main_loop(
                op_name, dfg_template, synthesis_res, K, stride, II
            )
            final_stream.extend(main_stream)

        # ── Case 2: r > 0，生成尾部标量展开 ────────────────────────
        if r > 0:
            base_addr_offset = K * stride   # 接着主循环的地址
            tail_stream = self._gen_tail(
                op_name, dfg_template, synthesis_res,
                r, base_addr_offset, stride,
                global_t_offset=K * II,
                II=II
            )
            final_stream.extend(tail_stream)

        # ── Case 3: K == 0，r > 0（N < vec_width）─纯尾部 ──────────
        # 已在 Case 2 覆盖

        print(f"[LOOP-STITCH] Generated {len(final_stream)} MIR ops "
              f"(main={K * len(dfg_template)}, tail={r * len(dfg_template)})")

        return self.base_stitcher.get_final_stream()

    # ──────────────────────────────────────────────────────────────────
    #  Private helpers
    # ──────────────────────────────────────────────────────────────────

    def _clone_dfg(self, dfg: List[MIROp], suffix: str) -> Tuple[List[MIROp], Dict[str, int]]:
        """
        深拷贝 DFG 并重命名所有 trace_id（加后缀避免冲突）。
        同时返回 trace_id 重映射表，用于更新 synthesis_res 的 key。
        """
        id_map: Dict[str, str] = {}
        for op in dfg:
            id_map[op.trace_id] = f"{op.trace_id}{suffix}"

        cloned = []
        for op in dfg:
            new_op = copy.deepcopy(op)
            new_op.trace_id = id_map[op.trace_id]

            # 更新 depends_on 引用
            old_deps = new_op.metadata.get("depends_on", [])
            new_op.metadata["depends_on"] = [
                id_map.get(d, d) for d in old_deps
            ]
            cloned.append(new_op)

        return cloned, id_map

    def _inject_loop_meta(
        self,
        dfg: List[MIROp],
        K: int,
        stride: int,
        addr_base: int = 0,
    ):
        """
        在主循环 DFG 的 I/O 节点注入 outer_loops / outer_inc。
        这些字段由 Compactor._pass1_linear_folding 识别后生成 LOOP_HDR。

        规则：
          - role == "operand_a" → 主输入，每轮 addr+stride
          - role == "output"    → 主输出，每轮 addr+stride
          - role == "constant"  → 常数，不步进（addr 固定）
        """
        for op in dfg:
            role = op.metadata.get("role", "")
            if role in ("operand_a", "output"):
                op.metadata["outer_loops"] = K - 1        # loops 字段 = 重复次数 - 1
                op.metadata["outer_inc"]   = stride       # 地址步进
                op.metadata["addr_base"]   = addr_base

    def _inject_addr_offset(self, dfg: List[MIROp], offset: int, stride: int = 1):
        """
        在尾部迭代 DFG 的 I/O 节点注入绝对地址偏移。
        """
        for op in dfg:
            role = op.metadata.get("role", "")
            if role in ("operand_a", "output"):
                existing = op.metadata.get("addr", 0) or 0
                op.metadata["addr"] = existing + offset
                # 尾部不带 loop 字段（单次发射）
                op.metadata.pop("outer_loops", None)
                op.metadata.pop("outer_inc", None)

    def _gen_main_loop(
        self,
        op_name: str,
        dfg: List[MIROp],
        synthesis_res: Dict[str, int],
        K: int,
        stride: int,
        II: int,
    ) -> List[MIROp]:
        """
        软件展开 K 次迭代（不使用 LOOP_HDR，避免时序错位）。

        策略：每次迭代克隆 DFG 并步进全部有物理地址的非常数节点，
             消除 w_sq 等中间 UR_WRITE 节点的 Bank 别名冒险。
             迭代间距使用完整流水延迟（latency），而非 II 吞吐量间距，
             确保软件展开共享寄存器时不发生总线晚期采样错误。
        """
        # ── Fix 2: 用完整流水延迟作迭代间距 ─────────────────────────
        latency = max(synthesis_res.values()) + 1 if synthesis_res else II
        print(f"[LOOP-STITCH] Using latency={latency} (II={II}) for SW-unroll gap")

        # ── Fix 1: 收集全部有物理 addr 的非常数节点基础地址 ──────────
        base_addrs: Dict[str, int] = {}
        for op in dfg:
            role = op.metadata.get("role", "")
            if role != "constant" and "addr" in op.metadata:
                base_addrs[op.trace_id] = op.metadata["addr"]
            # 清除旧的 outer_loops 元数据
            op.metadata.pop("outer_loops", None)
            op.metadata.pop("outer_inc", None)

        for k in range(K):
            if k == 0:
                # 第 0 次：直接用原始 DFG（地址不变），偏移=0
                self.base_stitcher.add_operator(
                    f"{op_name}_k0", dfg, synthesis_res,
                    force_min_offset=0
                )
            else:
                # 第 k 次：克隆 DFG + 步进所有非常数有地址节点
                cloned, id_map = self._clone_dfg(dfg, f"_k{k}")
                for op in cloned:
                    role = op.metadata.get("role", "")
                    # Fix 1: 所有有物理 addr 且非常数的节点都需要步进地址
                    if role != "constant" and "addr" in op.metadata and "bank_id" in op.metadata:
                        tid_base = op.trace_id.replace(f"_k{k}", "")
                        base_a = base_addrs.get(tid_base, op.metadata["addr"])
                        op.metadata["addr"] = base_a + k * stride
                    op.metadata.pop("outer_loops", None)
                    op.metadata.pop("outer_inc", None)
                new_res = {id_map.get(tid, tid): v for tid, v in synthesis_res.items()}
                # Fix 2: 用 latency 而非 II 作为软件展开的时间偏移
                self.base_stitcher.add_operator(
                    f"{op_name}_k{k}", cloned, new_res,
                    force_min_offset=k * latency
                )

        print(f"[LOOP-STITCH] Main loop: K={K} iters SW-unrolled, stride={stride}, latency={latency}")
        return dfg

    def _gen_tail(
        self,
        op_name: str,
        dfg_template: List[MIROp],
        synthesis_res: Dict[str, int],
        r: int,
        base_addr_offset: int,
        stride: int,
        global_t_offset: int = 0,
        II: int = 1,
    ) -> List[MIROp]:
        """
        生成尾部标量展开（r < vec_width 个元素）。
        策略：对每个余数元素，克隆 DFG 并指定物理地址，
              不注入 outer_loops（单次发射）。
        """
        tail_ops: List[MIROp] = []

        for i in range(r):
            suffix = f"_tail{i}"
            cloned, id_map = self._clone_dfg(dfg_template, suffix)

            # 地址偏移 = 主循环结束后的地址 + 当前余数索引
            # 注意：余数元素在同一 Bank Row 内（同一行的不同 lane），
            # 因此地址不变，只需要 MASK（此处简化：地址相同，广播读取，写回同行）
            addr_offset = base_addr_offset
            self._inject_addr_offset(cloned, addr_offset, stride)

            # 更新 synthesis_res 的 key
            new_res = {id_map.get(k, k): v for k, v in synthesis_res.items()}

            self.base_stitcher.add_operator(
                f"{op_name}_tail{i}", cloned, new_res,
                force_min_offset=global_t_offset + i * II
            )
            tail_ops.extend(cloned)

        print(f"[LOOP-STITCH] Tail: r={r} scalar iterations at addr_offset={base_addr_offset}")
        return tail_ops
