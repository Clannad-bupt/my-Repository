#!/usr/bin/env python3
"""步骤 4-5 验收(README §13):TilePlanner + SramAllocator C++ 自测驱动。

直接运行 backend_selftest 并透传退出码;输出包含两套 SRAM 配置下的 tile 方案。
"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.path.join(REPO, "build", "backend_selftest")


def test_backend_selftest():
    r = subprocess.run([BIN], capture_output=True, text=True)
    print(r.stdout, end="")
    if r.stderr:
        print(r.stderr, file=sys.stderr, end="")
    assert r.returncode == 0, "backend_selftest 失败"


if __name__ == "__main__":
    test_backend_selftest()
    print("PASS test_backend.py")
