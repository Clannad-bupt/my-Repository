#!/usr/bin/env python3
"""生成单个 N-tile 的 FPGA bundle（MM_CASE_IMAGE_ONLY 镜像生成 + 槽位重绑定段目录）。

用法: mk_tile_bundle.py <name> <M> <K> <N> <tdir> <tile>

产物 fpga_workspace/bundles/<name>/tile_XXXX/:
  segments/          ch{s}_{a,b_tensor,...}.hex + segments.json（已按槽位偏移重绑定，可整目录替换包内 hbm_data）
  golden/smc_*/output_bank/bank*_final.hex
  meta_v3.json
"""
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

TVGEN = Path("/home/wenyang.zhang/aiacc/ipset/1_ip/GPLPU/v1.0/3_tool/tvgen")
WK = TVGEN / "load_store_v3/MatMul_NT_w4f16/fpga_workspace"
GEN = WK / "bin/gen_w4f16_check"
PACKER_PATH = Path(
    "/home/wenyang.zhang/aiacc/ipset/1_ip/GPLPU/v1.0/2_verification/aiacc/testbench/sim/package_case_to_fpga.py")

_spec = importlib.util.spec_from_file_location("packer", str(PACKER_PATH))
packer = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(packer)


def main():
    name, tdir = sys.argv[1], sys.argv[5]
    m, k, n, tile = (int(sys.argv[i]) for i in (2, 3, 4, 6))
    case = WK / "cases" / "{}_img_t{}".format(name, tile)
    out = WK / "bundles" / name / "tile_{:04d}".format(tile)
    shutil.rmtree(str(case), ignore_errors=True)
    (case / "hbm_init").mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env.update(MM_M=str(m), MM_OUT_DTYPE="f16", MM_MULTISTAGE="1",
               MM_GROUP_SIZE="128", MM_N=str(n), K_TOTAL=str(k),
               MM_SLOT_MAP="fpga", MM_N_TILE_START=str(tile),
               MM_TENSOR_DIR=str(WK / "tensors" / tdir), MM_CASE_IMAGE_ONLY="1")
    r = subprocess.run([str(GEN), str(case), str(case / "hbm_init"), "42"],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    if r.returncode != 0 or not (case / "meta_v3.json").is_file():
        sys.exit("gen failed: {} tile {} (rc={})".format(name, tile, r.returncode))

    (case / "hbm_data").mkdir(exist_ok=True)
    for f in (case / "hbm_init").glob("memory_init*.hex"):
        shutil.copy2(str(f), str(case / "hbm_data" / f.name))
    if not packer.rebind_hbm_slots(case):
        sys.exit("rebind skipped (slot_map!=fpga): {} tile {}".format(name, tile))

    shutil.rmtree(str(out), ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    shutil.copytree(str(case / "hbm_data"), str(out / "segments"))
    shutil.copy2(str(case / "meta_v3.json"), str(out / "meta_v3.json"))
    for smc in sorted(case.glob("smc_*")):
        sub = smc / "output_bank"
        if not sub.is_dir():
            continue
        dst = out / "golden" / smc.name / "output_bank"
        dst.mkdir(parents=True, exist_ok=True)
        for f in sub.iterdir():
            shutil.copy2(str(f), str(dst / f.name))
    shutil.rmtree(str(case))
    print("[tile] {} tile {} OK".format(name, tile), flush=True)


if __name__ == "__main__":
    main()
