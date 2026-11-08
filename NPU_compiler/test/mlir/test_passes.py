#!/usr/bin/env python3
"""test/mlir 单元测试(README §11.1 单元级):minic 跑 pass + FileCheck 黄金比对。"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIC = os.path.join(REPO, "build", "minic")
FILECHECK = "/home/weizhi.zhou/llvm/build-mlir/bin/FileCheck"
CASES = [
    ("fusion.mlir", "fusion"),
    ("eliminate_transpose.mlir", "eliminate-transpose"),
    ("lowering.mlir", "func.func(minigraph-lower-to-mininpu)"),
    ("quant_lowering.mlir", "func.func(minigraph-lower-to-mininpu)"),
    ("fp8_lowering.mlir", "func.func(minigraph-lower-to-mininpu)"),
]

failed = 0
for fname, pipeline in CASES:
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), fname)
    r = subprocess.run([MINIC, "--input", path, "--run-pass", pipeline],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"FAIL {fname}: minic exit {r.returncode}\n{r.stderr}")
        failed += 1
        continue
    fc = subprocess.run([FILECHECK, path], input=r.stdout, capture_output=True, text=True)
    if fc.returncode != 0:
        print(f"FAIL {fname}: FileCheck\n{fc.stderr}\n---- minic output ----\n{r.stdout}")
        failed += 1
        continue
    print(f"PASS {fname} ({pipeline})")

sys.exit(1 if failed else 0)
