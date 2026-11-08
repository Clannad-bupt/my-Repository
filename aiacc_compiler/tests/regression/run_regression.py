#!/usr/bin/env python3
"""
AIACC 多算子回归门禁 (V2.0)

用法:
    python3 tests/regression/run_regression.py                           # 所有 ready 算子
    python3 tests/regression/run_regression.py --category unary          # 按类别过滤
    python3 tests/regression/run_regression.py --filter sin              # 按算子名过滤
    python3 tests/regression/run_regression.py --status pending --list   # 查看待实现算子
    python3 tests/regression/run_regression.py --verify-only             # 只验证不编译
    python3 tests/regression/run_regression.py --all                     # 包括 pending (会跳过)

目录约定:
    测试脚本: tests/operators/{category}/{op_name}/test_{op_name}.py
    Build:    build/operators/{category}/{op_name}/
"""
import os
import sys
import json
import time
import subprocess
import argparse
from pathlib import Path
from datetime import datetime

WS = Path(__file__).resolve().parent.parent.parent
REGRESSION_DIR = Path(__file__).resolve().parent


def load_registry():
    """加载 V2 注册表，展平为 operator 列表。"""
    reg_path = REGRESSION_DIR / "registry.json"
    if not reg_path.exists():
        print(f"❌ 注册表不存在: {reg_path}")
        sys.exit(1)

    with open(reg_path) as f:
        data = json.load(f)

    build_root = data.get("build_root", "build/operators")
    operators = []

    for category, cat_data in data.get("categories", {}).items():
        for op_name, op_info in cat_data.get("operators", {}).items():
            operators.append({
                "op_name": op_name,
                "category": category,
                "description": op_info.get("description", ""),
                "status": op_info.get("status", "pending"),
                # 约定推导路径
                "test_script": f"tests/operators/{category}/{op_name}/test_{op_name}.py",
                "build_dir": f"{build_root}/{category}/{op_name}",
                # 兼容: 支持 override
                **{k: v for k, v in op_info.items() if k in ("test_script", "build_dir")}
            })

    return operators, build_root


def run_compile(entry: dict) -> dict:
    """执行编译。支持通过 Template 回退执行。"""
    script = WS / entry["test_script"]
    cmd = [sys.executable, str(script)]
    
    # 模板回退逻辑
    is_template = False
    if not script.exists():
        category = entry["category"]
        template = None
        if category.startswith("unary/math"):
            template = WS / "tests/operators/unary/math/unary_test_template.py"
        elif category.startswith("binary/logical"):
            template = WS / "tests/operators/binary/logical/logical_test_template.py"
            
        if template and template.exists():
            cmd = [sys.executable, str(template), entry["op_name"]]
            is_template = True
        else:
            return {"passed": False, "duration": 0, 
                    "output": f"❌ 错误: 找不到测试脚本 {entry['test_script']}，且无可用模板"}

    t0 = time.time()
    try:
        if is_template:
            print(f" 💡 (Template: {entry['op_name']})", end="", flush=True)
            
        result = subprocess.run(
            cmd, capture_output=True, text=True, cwd=str(WS), timeout=120
        )
    except subprocess.TimeoutExpired:
        return {"passed": False, "duration": time.time() - t0, "output": "编译超时 (120s)"}

    dur = time.time() - t0
    passed = result.returncode == 0
    output = result.stdout[-3000:]
    if not passed:
        output += "\n--- STDERR ---\n" + result.stderr[-2000:]

    return {"passed": passed, "duration": dur, "output": output}


def run_verify(entry: dict) -> dict:
    """执行 debug_pipeline 5 阶段验证。"""
    build_dir = WS / entry["build_dir"]
    debug_script = WS / "src" / "tools" / "debug_pipeline.py"

    if not (build_dir / "stage_b_mapped.json").exists():
        return {"passed": False, "duration": 0,
                "output": f"编译产物不存在: {build_dir}"}

    t0 = time.time()
    try:
        result = subprocess.run(
            [sys.executable, str(debug_script), "--dir", str(build_dir)],
            capture_output=True, text=True, cwd=str(WS), timeout=60
        )
    except subprocess.TimeoutExpired:
        return {"passed": False, "duration": time.time() - t0, "output": "验证超时 (60s)"}

    dur = time.time() - t0
    return {"passed": result.returncode == 0, "duration": dur, "output": result.stdout}


def print_summary_table(results, show_category=True):
    """打印汇总表格。"""
    if show_category:
        print(f"\n  {'类别':<25} {'算子':<15} {'编译':>6} {'验证':>6} {'耗时':>8}")
        print(f"  {'─'*25} {'─'*15} {'─'*6} {'─'*6} {'─'*8}")
    else:
        print(f"\n  {'算子':<35} {'编译':>6} {'验证':>6} {'耗时':>8}")
        print(f"  {'─'*35} {'─'*6} {'─'*6} {'─'*8}")

    for r in results:
        c_icon = "✅" if r["compile"] else ("⏭️" if r.get("skipped") else "❌")
        v_icon = "✅" if r["verify"] else ("⏭️" if r.get("skipped") else "❌")
        dur = f"{r.get('duration', 0):.1f}s"

        if show_category:
            print(f"  {r.get('category', ''):<25} {r['op']:<15} {c_icon:>6} {v_icon:>6} {dur:>8}")
        else:
            print(f"  {r['op']:<35} {c_icon:>6} {v_icon:>6} {dur:>8}")


def main():
    parser = argparse.ArgumentParser(
        description="AIACC 多算子回归门禁 V2.0",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--category", "-c", default=None,
                        help="按类别过滤 (支持前缀匹配, 如 'unary' 或 'binary/math')")
    parser.add_argument("--filter", "-f", default=None,
                        help="按算子名过滤 (子串匹配)")
    parser.add_argument("--status", default="ready",
                        help="按状态过滤: ready/pending/all (默认 ready)")
    parser.add_argument("--verify-only", action="store_true",
                        help="只验证已有产物，不重编译")
    parser.add_argument("--list", action="store_true",
                        help="只列出算子，不执行")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="打印详细输出")
    parser.add_argument("--all", action="store_true",
                        help="包括所有状态的算子")
    args = parser.parse_args()

    operators, build_root = load_registry()

    # ─── 过滤 ───
    if not args.all:
        if args.status != "all":
            operators = [op for op in operators if op["status"] == args.status]
    if args.category:
        operators = [op for op in operators if op["category"].startswith(args.category)]
    if args.filter:
        operators = [op for op in operators if args.filter.lower() in op["op_name"].lower()]

    # ─── 列出模式 ───
    if args.list:
        print(f"\n{'类别':<25} {'算子':<15} {'状态':<8} {'描述'}")
        print(f"{'─'*25} {'─'*15} {'─'*8} {'─'*30}")
        for op in operators:
            status_icon = {"ready": "🟢", "pending": "⚪", "broken": "🔴"}.get(op["status"], "❓")
            print(f"{op['category']:<25} {op['op_name']:<15} {status_icon} {op['status']:<5} {op['description']}")

        ready = sum(1 for op in operators if op["status"] == "ready")
        pending = sum(1 for op in operators if op["status"] == "pending")
        print(f"\n总计: {len(operators)} 个算子 (🟢 ready={ready}, ⚪ pending={pending})")
        return

    if not operators:
        print("⚠️ 无匹配的算子 (提示: 使用 --status all 查看全部)")
        sys.exit(0)

    # ─── 执行 ───
    print("=" * 64)
    print("  AIACC Regression Gate V2.0")
    print(f"  时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"  算子数: {len(operators)} (status={args.status})")
    print(f"  Build Root: {build_root}/")
    print("=" * 64)

    results = []
    total_time = 0

    for i, entry in enumerate(operators):
        op_name = entry["op_name"]
        category = entry["category"]
        print(f"\n[{i+1}/{len(operators)}] {category}/{op_name}")
        print("─" * 40)

        # pending 算子跳过
        if entry["status"] == "pending":
            print("  ⏭️  状态 pending，跳过")
            results.append({"op": op_name, "category": category,
                            "compile": True, "verify": True, "skipped": True, "duration": 0})
            continue

        # 编译
        compile_ok = True
        if not args.verify_only:
            print("  ⚙️  编译中...", end="", flush=True)
            compile_res = run_compile(entry)
            total_time += compile_res["duration"]
            compile_ok = compile_res["passed"]

            if compile_ok:
                print(f" ✅ ({compile_res['duration']:.1f}s)")
            else:
                print(f" ❌ ({compile_res['duration']:.1f}s)")
                if args.verbose:
                    print(compile_res["output"])
                results.append({"op": op_name, "category": category,
                                "compile": False, "verify": False,
                                "duration": compile_res["duration"],
                                "detail": compile_res["output"]})
                continue

        # 验证
        print("  🔍 验证中...", end="", flush=True)
        verify_res = run_verify(entry)
        total_time += verify_res["duration"]

        if verify_res["passed"]:
            print(f" ✅ ({verify_res['duration']:.1f}s)")
        else:
            print(f" ❌ ({verify_res['duration']:.1f}s)")

        if args.verbose or not verify_res["passed"]:
            for line in verify_res["output"].split("\n"):
                if "❌" in line or "🎯" in line or "💡" in line:
                    print(f"    {line.strip()}")

        results.append({
            "op": op_name, "category": category,
            "compile": compile_ok, "verify": verify_res["passed"],
            "duration": total_time,
            "detail": verify_res["output"] if not verify_res["passed"] else ""
        })

    # ─── 汇总 ───
    print("\n" + "=" * 64)
    print("  回归报告")
    print("=" * 64)

    active = [r for r in results if not r.get("skipped")]
    passed = sum(1 for r in active if r["verify"])
    failed = len(active) - passed
    skipped = sum(1 for r in results if r.get("skipped"))

    print_summary_table(active, show_category=len(set(r["category"] for r in active)) > 1)

    summary = f"\n  活跃: {passed}/{len(active)} 通过"
    if failed:
        summary += f", {failed} 失败"
    if skipped:
        summary += f" | 跳过: {skipped} pending"
    summary += f" | 耗时 {total_time:.1f}s"
    print(summary)

    if failed > 0:
        print(f"\n  ❌ 失败算子:")
        for r in active:
            if not r["verify"]:
                print(f"    - {r['category']}/{r['op']}")
                for line in r.get("detail", "").split("\n"):
                    if "🎯" in line:
                        print(f"      {line.strip()}")
                        break

    print("\n" + "=" * 64)

    # 保存报告
    report_path = REGRESSION_DIR / "last_report.json"
    with open(report_path, "w") as f:
        json.dump({
            "timestamp": datetime.now().isoformat(),
            "total": len(results), "active": len(active),
            "passed": passed, "failed": failed, "skipped": skipped,
            "duration": total_time,
            "results": [{k: v for k, v in r.items() if k != "detail"} for r in results]
        }, f, indent=2, ensure_ascii=False)
    print(f"  📄 报告: {report_path}")

    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
