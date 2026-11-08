#!/usr/bin/env python3
"""
AIACC 一键 Debug 入口 (V1.0)

用法:
    python3 src/tools/debug_pipeline.py --op sin_taylor_high_order
    python3 src/tools/debug_pipeline.py --op sin_taylor_high_order --build-dir build/sin8
    python3 src/tools/debug_pipeline.py --op sin_taylor_high_order --recompile

功能:
    自动执行 编译(可选) → 逐阶段验证 → 精确定位到 Bug 所在阶段/节点
    一条命令完成从「测试失败」到「定位根因」的全流程。

阶段定义:
    [A] Frontend DFG 拓扑与数学正确性
    [C] SMT 时序延迟约束 & ALU Liveness
    [B] Bank 冲突 & IQ ID 分配
    [D] Hex 文件完整性 & 常量装载对齐
    [E] 逐周期仿真对比 (S0/S1/Data)
"""
import os
import sys
import json
import argparse
import struct
import re
import traceback
from pathlib import Path
import numpy as np

# 确保 src 在 path
ws = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ws / "src"))

from tools.dfg_evaluator import eval_dfg, parse_deps, verify_dfg_topology


# ═══════════════════════════════════════════════════════════════
# Hex 工具
# ═══════════════════════════════════════════════════════════════

def hex128_to_float_lane0(hex_str: str) -> float:
    """128-bit hex → Lane 0 (最低 32 bits) → float32."""
    hex_str = str(hex_str).strip().replace("0x", "")
    if not hex_str or hex_str == '0' * 32:
        return 0.0
    padded = hex_str.zfill(32)
    lane_hex = padded[-8:]
    try:
        return struct.unpack('>f', bytes.fromhex(lane_hex))[0]
    except Exception:
        return 0.0


# ═══════════════════════════════════════════════════════════════
# 阶段验证器
# ═══════════════════════════════════════════════════════════════

class StageResult:
    """单个阶段的验证结果。"""
    def __init__(self, name: str):
        self.name = name
        self.passed = True
        self.errors = []
        self.warnings = []

    def fail(self, msg: str):
        self.passed = False
        self.errors.append(msg)

    def warn(self, msg: str):
        self.warnings.append(msg)

    def __str__(self):
        icon = "✅" if self.passed else "❌"
        return f"{icon} [{self.name}] {'PASS' if self.passed else f'FAIL ({len(self.errors)} errors)'}"


def check_stage_a(dfg, input_data):
    """[Stage A] DFG 拓扑连通性 + 数学正确性。"""
    r = StageResult("A: DFG 数学与拓扑")

    # 拓扑检查
    topo_errs = verify_dfg_topology(dfg)
    for e in topo_errs:
        r.fail(f"拓扑断裂: {e}")

    # 数学顺推
    val_map = eval_dfg(dfg, input_data)

    # 检查常量节点是否正确求值
    for n in dfg:
        tid = n["trace_id"]
        meta = n.get("metadata", {})
        if n["op_type"] == "UR_READ" and str(meta.get("is_const", "")).lower() == "true":
            expected = float(meta.get("value", 0))
            actual = val_map.get(tid, None)
            if actual is None or not np.isclose(expected, actual, atol=1e-10, equal_nan=True):
                r.fail(f"常量 {tid}: 期望={expected}, eval_dfg={actual}")

    # 检查输出节点是否存在
    outputs = [n for n in dfg if n["op_type"] == "UR_WRITE"]
    if not outputs:
        r.fail("DFG 缺少 UR_WRITE 输出节点")

    return r, val_map


def check_stage_c(dfg, spec_delays):
    """[Stage C] SMT 时序约束 + Liveness 检测。"""
    r = StageResult("C: SMT 时序与 Liveness")

    nodes = {n["trace_id"]: n for n in dfg if n.get("cycle") is not None}

    # 延迟约束检查
    for n in dfg:
        cyc = n.get("cycle")
        if cyc is None:
            continue
        meta = n.get("metadata", {})
        for dep_id in parse_deps(meta):
            if dep_id not in nodes:
                continue
            dep = nodes[dep_id]
            dep_cyc = dep.get("cycle")
            dep_type = dep.get("op_type", "").lower()
            lat = spec_delays.get(dep_type, 1)
            if dep_type == "rtovr":
                lat = 1
            if cyc < dep_cyc + lat:
                r.fail(f"延迟违反: {n['trace_id']}(T={cyc}) < {dep_id}(T={dep_cyc}) + latency({lat})")

    # Liveness 检查
    alu_execs = {"FPMUL": [], "FPADD": []}
    for n in dfg:
        if n["op_type"] in alu_execs and n.get("cycle") is not None:
            alu_execs[n["op_type"]].append((n["cycle"], n["trace_id"]))
    for t in alu_execs:
        alu_execs[t].sort()

    for n in dfg:
        if n["op_type"] not in ["FPMUL", "FPADD"] or n.get("cycle") is None:
            continue
        t_prod = n["cycle"]
        # 🛸 [V2.1 物理流水线对齐] ALU 流水线延迟：结果在 T_fire + lat 才写回 D 寄存器
        prod_lat = spec_delays.get(n["op_type"], 4)
        for consumer in dfg:
            if n["trace_id"] in parse_deps(consumer.get("metadata", {})):
                if consumer.get("op_type") != "RTOVR": continue  # 只检查真正搬运消费者
                t_cons = consumer.get("cycle", -1)
                if t_cons is not None and t_cons > t_prod:
                    for exec_t, exec_id in alu_execs[n["op_type"]]:
                        # 物理判据：覆写发生当且仅当新结果的写回时间 (exec_t + lat)
                        # 早于或等于当前数据被搬走的时间 (t_cons)
                        writeback_t = exec_t + prod_lat
                        if t_prod < exec_t and writeback_t <= t_cons and exec_id != n["trace_id"]:
                            r.fail(f"Liveness 覆写: {n['trace_id']}(T={t_prod}) → "
                                   f"{consumer['trace_id']}(T={t_cons}), "
                                   f"被 {exec_id}(T={exec_t}, 写回T={writeback_t}) 覆写!")

    return r


def check_stage_b(dfg):
    """[Stage B] Bank 冲突 + IQ ID 分配完整性。"""
    r = StageResult("B: Bank 分配与 IQ 完整性")

    # Bank 冲突检查
    bank_usage = {}  # bank_id -> [(cycle, node_id)]
    for n in dfg:
        meta = n.get("metadata", {})
        b = meta.get("bank_id")
        cyc = n.get("cycle")
        if b is not None and cyc is not None:
            bank_usage.setdefault(str(b), []).append((int(cyc), n["trace_id"]))

    for bid, entries in bank_usage.items():
        entries.sort()
        for i in range(len(entries) - 1):
            if entries[i + 1][0] - entries[i][0] < 1:
                r.fail(f"Bank {bid} 时间冲突: "
                       f"{entries[i][1]}(T={entries[i][0]}) & "
                       f"{entries[i + 1][1]}(T={entries[i + 1][0]})")

    # IQ ID 一致性: 检查 UR_WRITE 和其 RTOVR 源的 slot_id 是否对齐
    nodes_by_id = {n["trace_id"]: n for n in dfg}
    for n in dfg:
        if n["op_type"] in ["FPMUL", "FPADD", "UR_WRITE", "UR_READ"]:
            iq_id = n.get("metadata", {}).get("iq_id")
            if iq_id is None:
                r.warn(f"节点 {n['trace_id']} 缺少 iq_id 分配")

    return r


def check_stage_d(op_dir):
    """[Stage D] Hex 文件完整性 + 常量装载对齐 + 指令反汇编验证。"""
    r = StageResult("D: Hex 完整性与常量对齐")

    acb = op_dir / "acb_bundle"
    smc0 = acb / "smc_0"

    # sim_config.json 存在性
    sim_conf = acb / "sim_config.json"
    if not sim_conf.exists():
        r.fail("sim_config.json 不存在")
        return r

    with open(sim_conf) as f:
        conf = json.load(f)

    # 所有 IQ hex 文件存在性
    for iq_name, iq_path in conf.get("iq_init", {}).items():
        full = acb / iq_path
        if not full.exists():
            r.fail(f"IQ Hex 文件缺失: {iq_path}")

    # 常量 Bank 对齐检查
    stage_b = op_dir / "stage_b_mapped.json"
    if stage_b.exists():
        with open(stage_b) as f:
            dfg = json.load(f)

        for n in dfg:
            meta = n.get("metadata", {})
            if n["op_type"] != "UR_READ" or str(meta.get("is_const", "")).lower() != "true":
                continue
            b_id = meta.get("bank_id")
            addr = meta.get("addr")
            if b_id is None or addr is None:
                continue

            hex_path = smc0 / "ur_data" / f"bank{int(b_id)}.hex"
            if not hex_path.exists():
                r.fail(f"Bank {b_id} hex 不存在: {hex_path}")
                continue

            with open(hex_path) as f:
                lines = [l.strip() for l in f if l.strip()]
            a = int(addr)
            if a < len(lines):
                real_val = hex128_to_float_lane0(lines[a].replace("0x", ""))
                expected = float(meta["value"])
                if not np.isclose(expected, real_val, atol=1e-4, equal_nan=True):
                    r.fail(f"常量 {n['trace_id']}: Bank{b_id}[{a}] "
                           f"期望={expected:.6f}, 实际={real_val:.6f}")

    # 指令反汇编验证: 检查所有 IQ hex 是否可正确解码
    try:
        from tools.hex_disassembler import disassemble_iq
        from aiacc_hardware.src.spec_loader import SpecLoader
        spec_loader = SpecLoader()

        for iq_name, iq_path in conf.get("iq_init", {}).items():
            hex_file = acb / iq_path
            if not hex_file.exists():
                continue
            results = disassemble_iq(iq_name, hex_file, spec_loader)
            for pc, hex_val, asm, _ in results:
                if "⚠️" in asm:
                    r.fail(f"IQ {iq_name} PC={pc}: 指令解码异常 — {asm}")
    except ImportError:
        r.warn("hex_disassembler 不可用，跳过指令反汇编验证")

    return r


def check_stage_e(op_dir, val_map):
    """[Stage E] 逐周期仿真对比 — 使用影子寄存器同步模型。"""
    from tools.cycle_verifier import CycleVerifier
    r = StageResult("E: 仿真逐周期对比")
    
    try:
        verifier = CycleVerifier(op_dir)
        errors = verifier.run()
        if errors > 0:
            r.fail(f"物理寄存器同步失败, 共发现 {errors} 个偏差")
    except Exception as e:
        r.fail(f"加载物理验证器失败: {e}")

    # 🛸 [BUG-FIX] 恢复被 return 截断的 Golden 对比逻辑
    sim_log_path = op_dir / "sim.log"
    if sim_log_path.exists():
        with open(sim_log_path) as f:
            log_content = f.read()
            if "PASSED" in log_content or "Verified successfully" in log_content:
                pass
            else:
                r.fail("Golden 对比未通过 (Max Diff 校验失败)")
    else:
        r.fail("sim.log 不存在，无法进行 Golden 校验")

    return r


# ═══════════════════════════════════════════════════════════════
# 主入口
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="AIACC 一键 Debug 入口 — 自动逐阶段定位编译器 Bug",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 新约定路径 (推荐)
  python3 src/tools/debug_pipeline.py --op sin --category unary/transcendentals
  python3 src/tools/debug_pipeline.py --op sin --category unary/transcendentals --recompile

  # 直接指定目录 (兼容旧结构)
  python3 src/tools/debug_pipeline.py --dir build/sin8/sin_taylor_high_order
  python3 src/tools/debug_pipeline.py --dir build/operators/unary/transcendentals/sin
        """
    )
    parser.add_argument("--op", default="sin", help="算子名称")
    parser.add_argument("--category", "-c", default=None,
                        help="算子类别路径 (如 unary/transcendentals, activation)")
    parser.add_argument("--build-root", default="build/operators", help="Build 根目录")
    parser.add_argument("--dir", default=None, help="直接指定已编译的输出目录 (跳过路径推导)")
    parser.add_argument("--recompile", action="store_true", help="强制重新编译后再验证")
    args = parser.parse_args()

    if args.dir:
        op_dir = Path(args.dir)
    elif args.category:
        op_dir = Path(args.build_root) / args.category / args.op
    else:
        # 自动搜索: 在 build/operators 下递归查找
        candidates = list(Path(args.build_root).rglob(f"{args.op}/stage_b_mapped.json"))
        if candidates:
            op_dir = candidates[0].parent
        else:
            op_dir = Path(args.build_root) / args.op

    print("=" * 64)
    print("  AIACC Debug Pipeline V2.0 — 逐阶段 Bug 定位器")
    print(f"  Target: {op_dir}")
    print("=" * 64)

    # ─── 预检:是否需要重编译 ───
    if args.recompile or not (op_dir / "stage_b_mapped.json").exists():
        print("\n⚙️  执行编译...")
        try:
            # 按约定路径查找测试脚本
            test_script = None
            if args.category:
                conv_path = Path("tests/operators") / args.category / args.op / f"test_{args.op}.py"
                if conv_path.exists():
                    test_script = conv_path

            # 回退: 递归搜索
            if not test_script:
                candidates = list(Path("tests/operators").rglob(f"test_{args.op}.py"))
                if not candidates:
                    candidates = list(Path("tests/operators").rglob(f"*{args.op}*frontend*.py"))
                test_script = candidates[0] if candidates else None

            if test_script and test_script.exists():
                import subprocess
                result = subprocess.run(
                    [sys.executable, str(test_script)],
                    capture_output=True, text=True, cwd=str(ws)
                )
                if result.returncode != 0:
                    print(f"  ❌ 编译失败! 错误输出:")
                    print(result.stderr[-2000:] if len(result.stderr) > 2000 else result.stderr)
                    sys.exit(1)
                print("  ✅ 编译完成")
            else:
                print(f"  ⚠️ 未找到编译脚本，跳过编译阶段")
        except Exception as e:
            print(f"  ❌ 编译异常: {e}")
            traceback.print_exc()
            sys.exit(1)

    # ─── 加载数据 ───
    stage_b_path = op_dir / "stage_b_mapped.json"
    if not stage_b_path.exists():
        print(f"\n❌ 编译产物不存在: {stage_b_path}")
        print("   请先运行编译或使用 --recompile 选项")
        sys.exit(1)

    with open(stage_b_path) as f:
        dfg = json.load(f)

    # 识别输出名称 (从 DFG metadata 中获取)
    out_name = "y" # 默认值
    for n in dfg:
        if n.get("metadata", {}).get("role") == "output":
            out_name = n.get("metadata", {}).get("meta_name", "out")
            break

    # 加载输入数据
    input_data = {}
    golden_path = op_dir / "acb_bundle" / "golden_data.npz"
    if golden_path.exists():
        data = np.load(golden_path)
        for k in data.files:
            # 修改：只过滤掉真正的输出字段，而不应硬编码过滤 "y"
            if k != "y" and k != out_name:
                input_data[k] = data[k]
            # 兼容：如果 k 是 y 且 dfg 中标记 y 是输入，则保留
            elif k == "y":
                is_y_input = any(n.get("metadata", {}).get("meta_name") == "y" and n.get("metadata", {}).get("role") != "output" for n in dfg)
                if is_y_input:
                    input_data[k] = data[k]
    if not input_data:
        input_data = {"x": 1.5668}

    # 加载延迟规格
    spec_delays = {}
    try:
        import yaml
        delays_path = ws / "hw_config" / "pipeline_delays.yaml"
        if delays_path.exists():
            with open(delays_path) as f:
                spec_delays = yaml.safe_load(f).get("pipeline_delays", {})
    except Exception:
        spec_delays = {"fpmul": 5, "fpadd": 4, "ur_read": 1, "ur_write": 1, "rtovr": 1}

    # ─── 逐阶段验证 ───
    results = []
    first_fail = None

    # Stage A
    print("\n" + "─" * 50)
    ra, val_map = check_stage_a(dfg, input_data)
    results.append(ra)
    print(f"  {ra}")
    for e in ra.errors[:3]:
        print(f"    → {e}")
    if not ra.passed and not first_fail:
        first_fail = ra

    # Stage C
    rc = check_stage_c(dfg, spec_delays)
    results.append(rc)
    print(f"  {rc}")
    for e in rc.errors[:3]:
        print(f"    → {e}")
    if not rc.passed and not first_fail:
        first_fail = rc

    # Stage B
    rb = check_stage_b(dfg)
    results.append(rb)
    print(f"  {rb}")
    for e in rb.errors[:3]:
        print(f"    → {e}")
    if not rb.passed and not first_fail:
        first_fail = rb

    # Stage D
    rd = check_stage_d(op_dir)
    results.append(rd)
    print(f"  {rd}")
    for e in rd.errors[:3]:
        print(f"    → {e}")
    if not rd.passed and not first_fail:
        first_fail = rd

    # Stage E
    re_result = check_stage_e(op_dir, val_map)
    results.append(re_result)
    print(f"  {re_result}")
    for e in re_result.errors[:5]:
        print(f"    → {e}")
    if not re_result.passed and not first_fail:
        first_fail = re_result

    # ─── 汇总报告 ───
    print("\n" + "=" * 64)
    total_pass = sum(1 for r in results if r.passed)
    total_warn = sum(len(r.warnings) for r in results)
    total_err = sum(len(r.errors) for r in results)

    if total_err == 0:
        print(f"  🚀 全部 {total_pass}/{len(results)} 阶段通过!")
        if total_warn:
            print(f"  ⚠️  {total_warn} 个警告 (不影响功能)")
    else:
        print(f"  ❌ {len(results) - total_pass}/{len(results)} 阶段失败, 共 {total_err} 个错误")
        if first_fail:
            print(f"\n  🎯 Bug 定位: 首个失败阶段 = [{first_fail.name}]")
            print(f"     首个错误: {first_fail.errors[0]}")
            print(f"\n  💡 建议:")
            if "A:" in first_fail.name:
                print("     检查 src/frontend/compiler.py 的表达式展开逻辑")
            elif "C:" in first_fail.name:
                print("     检查 src/synthesis/backend.py 的 SMT 约束或 axioms.py")
            elif "B:" in first_fail.name:
                print("     检查 src/tools/standard_pipeline_runner.py 的物理分配器")
            elif "D:" in first_fail.name:
                print("     检查 src/driver/emitter.py 的 Hex 打包逻辑")
            elif "E:" in first_fail.name:
                print("     运行 cycle_verifier.py 查看详细逐周期对比:")
                print(f"     python3 src/tools/cycle_verifier.py --dir {op_dir}")

    print("=" * 64)
    sys.exit(0 if total_err == 0 else 1)


if __name__ == "__main__":
    main()
