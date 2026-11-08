#!/usr/bin/env python3
"""
AIACC Hex 指令反汇编器 (V1.0)

用法:
    python3 src/tools/hex_disassembler.py --dir build/operators/unary/transcendentals/sin
    python3 src/tools/hex_disassembler.py --dir build/operators/unary/transcendentals/sin --iq fpmul
    python3 src/tools/hex_disassembler.py --dir build/operators/unary/transcendentals/sin --compare

功能:
    将 acb_bundle/smc_0/iq_*.hex 反解为人类可读的伪指令序列。
    精确区分 Stage D (Hex 打包) 问题 vs Stage E (模拟器执行) 问题。

输出示例:
    ── IQ: fpmul (iq_fpmul.hex, 18 lines) ──
    PC=0  0x000000000  NOP (DLY=3)
    PC=1  0x100380004  VALID SRC_PREC=PREC_32 DST_PREC=PREC_32 (Cycle≈4)
    PC=2  0x000000000  NOP (DLY=4)
    PC=3  0x100380004  VALID SRC_PREC=PREC_32 DST_PREC=PREC_32 (Cycle≈13)
"""
import sys
import json
import argparse
from pathlib import Path

# 工程路径
ws = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ws / "src"))
sys.path.insert(0, str(ws / "sim"))

from aiacc_hardware.src.spec_loader import SpecLoader
from smc_simulator.core.decoder import InstructionDecoder


# IQ 名称到硬件单元类型的映射
def iq_name_to_unit_type(iq_name: str) -> str:
    """将 IQ 名称映射到硬件单元类型 (用于加载 spec)。"""
    name = iq_name.lower()
    if name.startswith("ur-r") or name.startswith("ur_r"):
        return "UR_READ"
    elif name.startswith("ur-w") or name.startswith("ur_w"):
        return "UR_WRITE"
    elif name.startswith("rtovr"):
        return "RTOVR"
    elif "fpmul" in name:
        return "FPMUL"
    elif "fpadd" in name:
        return "FPADD"
    elif "logic" in name:
        return "LOGIC"
    elif "intadd" in name:
        return "INTADD"
    else:
        return name.upper()


def format_decoded(decoded_dict: dict, unit_type: str) -> str:
    """将解码结果格式化为简洁的伪指令字符串。"""
    parts = []

    # VLD 状态
    vld = decoded_dict.get("VLD", 0)
    if isinstance(vld, str):
        is_valid = vld.upper() == "VALID" or vld == "1"
    else:
        is_valid = bool(vld)

    if not is_valid:
        # NOP 指令: 检查 DLY
        dly = decoded_dict.get("DLY", 0)
        loops = decoded_dict.get("LOOPS", 0)
        if isinstance(dly, str):
            try: dly = int(dly)
            except: dly = 0
        if isinstance(loops, str):
            try: loops = int(loops)
            except: loops = 0

        nop_info = []
        if dly > 0:
            nop_info.append(f"DLY={dly}")
        if loops > 0:
            nop_info.append(f"LOOPS={loops}")
        return f"NOP ({', '.join(nop_info)})" if nop_info else "NOP"

    parts.append("VALID")

    # Header 字段
    for hf in ["LOOPS", "DLY", "INC", "EMBED_LOOPS", "JUMP", "COND"]:
        v = decoded_dict.get(hf, 0)
        if isinstance(v, str):
            try: v = int(v)
            except: pass
        if v and v != 0:
            parts.append(f"{hf}={v}")

    # Payload 字段 (按单元类型过滤显示)
    skip_keys = {"VLD", "LOOPS", "DLY", "INC", "EMBED_LOOPS", "EMBED_END",
                 "INC_EMBED", "JUMP", "COND"}

    for k, v in decoded_dict.items():
        if k in skip_keys:
            continue
        if v in (0, "0", False, None, ""):
            continue
        # 简化显示
        parts.append(f"{k}={v}")

    return " ".join(parts)


def disassemble_iq(iq_name: str, hex_path: Path, spec_loader: SpecLoader) -> list:
    """反汇编单个 IQ 的 hex 文件。返回 [(pc, hex_val, asm_str, decoded_dict), ...]"""
    unit_type = iq_name_to_unit_type(iq_name)

    try:
        spec = spec_loader.get_unit(unit_type.lower())
        if spec is None:
            # 某些 RTOVR 用通用 spec
            spec = spec_loader.get_unit("rtovr")
    except Exception:
        spec = None

    if spec is None:
        return [(0, 0, f"⚠️ 无法加载 {unit_type} 的硬件 spec", {})]

    decoder = InstructionDecoder(spec)

    results = []
    with open(hex_path, 'r') as f:
        for pc, line in enumerate(f):
            line = line.strip()
            if not line:
                continue

            try:
                hex_val = int(line.replace("0x", ""), 16)
            except ValueError:
                results.append((pc, 0, f"⚠️ 无法解析: {line}", {}))
                continue

            if hex_val == 0:
                results.append((pc, 0, "NOP (zeros)", {}))
                continue

            try:
                decoded = decoder.decode(hex_val)
                d = decoded.to_dict()
                asm = format_decoded(d, unit_type)
                results.append((pc, hex_val, asm, d))
            except Exception as e:
                results.append((pc, hex_val, f"⚠️ 解码异常: {e}", {}))

    return results


def run_disassembler(op_dir: Path, iq_filter: str = None, compare_dfg: bool = False):
    """主反汇编流程。"""
    acb = op_dir / "acb_bundle"
    sim_conf_path = acb / "sim_config.json"

    if not sim_conf_path.exists():
        print(f"❌ sim_config.json 不存在: {sim_conf_path}")
        return False

    with open(sim_conf_path) as f:
        config = json.load(f)

    iq_init = config.get("iq_init", {})
    spec_loader = SpecLoader()

    # 加载 DFG 用于交叉对比 (可选)
    dfg_timeline = {}
    if compare_dfg:
        stage_b = op_dir / "stage_b_mapped.json"
        if stage_b.exists():
            with open(stage_b) as f:
                dfg = json.load(f)
            for n in dfg:
                cyc = n.get("cycle")
                if cyc is not None:
                    dfg_timeline.setdefault(int(cyc), []).append(n)

    print("=" * 72)
    print("  AIACC Hex Disassembler V1.0")
    print(f"  Target: {op_dir}")
    print("=" * 72)

    all_ok = True

    for iq_name, iq_path in sorted(iq_init.items()):
        if iq_filter and iq_filter.lower() not in iq_name.lower():
            continue

        hex_file = acb / iq_path
        if not hex_file.exists():
            print(f"\n  ❌ {iq_name}: 文件不存在 {hex_file}")
            all_ok = False
            continue

        unit_type = iq_name_to_unit_type(iq_name)
        results = disassemble_iq(iq_name, hex_file, spec_loader)

        # 统计
        valid_count = sum(1 for _, _, asm, _ in results if asm.startswith("VALID"))
        nop_count = sum(1 for _, _, asm, _ in results if asm.startswith("NOP"))

        print(f"\n── IQ: {iq_name} ({iq_path}, {len(results)} lines, "
              f"{valid_count} active, {nop_count} NOP) ──")
        print(f"  {'PC':>4}  {'Hex':<22} {'伪指令'}")
        print(f"  {'─'*4}  {'─'*22} {'─'*45}")

        for pc, hex_val, asm, d_dict in results:
            hex_str = f"0x{hex_val:016x}" if hex_val else "0x0"
            # 截断超长 hex
            if len(hex_str) > 22:
                hex_str = hex_str[:19] + "..."

            marker = ""
            if "⚠️" in asm:
                marker = " ←⚠️"
                all_ok = False

            print(f"  {pc:4d}  {hex_str:<22} {asm}{marker}")

    # DFG 交叉对比
    if compare_dfg and dfg_timeline:
        print(f"\n── DFG 时序 vs Hex 指令交叉参考 ──")
        print(f"  {'Cycle':>5}  {'DFG 算子':<35} {'类型':<10}")
        print(f"  {'─'*5}  {'─'*35} {'─'*10}")
        for cyc in sorted(dfg_timeline.keys()):
            for n in dfg_timeline[cyc]:
                print(f"  {cyc:5d}  {n['trace_id']:<35} {n['op_type']:<10}")

    print("\n" + "=" * 72)
    if all_ok:
        print("  ✅ 所有 IQ Hex 文件反汇编正常")
    else:
        print("  ⚠️ 部分 IQ 存在解码异常，请检查 Stage D 打包逻辑")
    print("=" * 72)

    return all_ok


def main():
    parser = argparse.ArgumentParser(
        description="AIACC Hex 指令反汇编器 — 将 .hex 反解为人类可读伪指令",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python3 src/tools/hex_disassembler.py --dir build/operators/unary/transcendentals/sin
  python3 src/tools/hex_disassembler.py --dir build/operators/unary/transcendentals/sin --iq fpmul
  python3 src/tools/hex_disassembler.py --dir build/operators/unary/transcendentals/sin --compare
        """
    )
    parser.add_argument("--dir", required=True, help="算子 build 目录")
    parser.add_argument("--iq", default=None, help="只显示指定 IQ (子串匹配, 如 fpmul)")
    parser.add_argument("--compare", action="store_true", help="同时显示 DFG 时序参照")
    args = parser.parse_args()

    ok = run_disassembler(Path(args.dir), iq_filter=args.iq, compare_dfg=args.compare)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
