#!/usr/bin/env python3
"""
AIACC V5.0 软件详细设计报告生成器

小文件是"源码"（可安全编辑），大文件是"编译产物"（永远不手动编辑）。

用法: python3 build_report.py
输出: build/aiacc_compiler_v5_design_report.md
"""

import os
from datetime import datetime

BASE = os.path.dirname(os.path.abspath(__file__))
DOCS_DIR = os.path.join(BASE, "docs")
OUTPUT_DIR = os.path.join(BASE, "build")
OUTPUT = os.path.join(OUTPUT_DIR, "aiacc_compiler_v5_design_report.md")

SECTIONS = [
    ("# AIACC V5.0 编译器详细设计报告\n\n"
     "> **本文档由 `build_report.py` 自动生成，禁止手动编辑。**\n"
     "> **修改请到 `docs/` 下的对应小文件中进行。**\n"),

    # Part 1: 硬件物理规格 (HCO)
    "hco/detail_hw_spec.md",

    # Part 2: 架构设计 (ADM)
    "adm/baseline.md",
    "adm/detail_ir_pipeline.md",
    "adm/detail_synthesis.md",
    "adm/detail_hsts_hazard.md",
    "adm/detail_topology.md",

    # Part 3: 架构决策记录
    "adm/adr/ADR-001_rtovr.md",
    "adm/adr/ADR-002_bank_allocation.md",
    "adm/adr/ADR-003_lowering.md",

    # Part 4: 路线图
    "adm/roadmap.md",

    # Part 5: 软件工程 (SE)
    "se/source_structure.md",
    "se/module_contracts.md",

    # Part 6: 测试与验证 (TFD)
    "tfd/tdd_spec.md",
    "tfd/verification.md",
    "tfd/detail_guards.md",
    "tfd/phase1_hw_contract.md",
    "tfd/phase2_ir.md",
    "tfd/phase3_synthesis.md",
    "tfd/phase4_orchestrator.md",
    "tfd/phase5_layout_driver.md",
    "tfd/phase6_integration.md",

    # Part 6: 验证基准 (PVO)
    "pvo/golden_baseline.md",

    # Part 7: 门禁清单 (IG)
    "ig/gate_checklist.md",
]


def build():
    parts = []
    for section in SECTIONS:
        if section.startswith("#") or section.startswith(">"):
            parts.append(section)
        else:
            path = os.path.join(DOCS_DIR, section)
            if os.path.exists(path):
                with open(path, "r", encoding="utf-8") as f:
                    parts.append(f"\n---\n\n{f.read().strip()}\n")
            else:
                parts.append(f"\n---\n\n> ⚠️ 缺失: `{section}`\n")

    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    parts.append(f"\n---\n\n*自动生成于 {timestamp}*\n")

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8") as f:
        f.write("\n".join(parts))

    print(f"✅ 报告: {OUTPUT}")
    print(f"   行数: {open(OUTPUT).read().count(chr(10))}")


if __name__ == "__main__":
    build()
