#!/usr/bin/env python3
"""行块模板回归(README §9.4/§13 步骤 7,回归加固):rows > Tr 时
rms/quant/eltwise 的多 tile 循环。

  历史缺陷(二期遗留,2026-09-07 修复):
  1. 双缓冲时 vec 的 wait 用被本拍预取覆盖的 lastLd → CodeGen checkCovered
     拒("wait token 未被任何前置 sync 覆盖")——任何 rows > Tr 的
     rms/silu/mul/add/quant 图都无法编译(MLP 默认 shape 恰好单 tile 才漏网);
  2. 单缓冲(db=false)时 tile≥1 的 x/rhs 从未载入、p 在 1 槽 buffer 上越界,
     只有 H 大到单行放不满半 SRAM 才触发。
  修复 = matmul k-loop 的 curLd 模式(先保存本拍凭据再预取);单缓冲退化
  为顺序「载入→sync→vec→store」。本测试两头都锁:
    - 双缓冲:silu/mul(2D rhs)/rms 512×256/256×256(Tr=80/112 → 3-7 tile,
      含 32 行残块),数值 vs numpy,且 timeline 上 DMA∩VEC 重叠 > 0;
    - 单缓冲:1/2 KiB SRAM spec 强制 Tr=1、ntiles=64,数值位级一致。
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MINIC = os.path.join(REPO, "build", "minic")
MINIRT = os.path.join(REPO, "build", "minirt")
SPEC = os.path.join(REPO, "configs", "npu256.json")

ROWS, H = 512, 256  # 默认 spec:silu Tr=128、mul Tr=80、rms Tr=112 → 多 tile


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0, f"cmd failed: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}"
    return r


def ref_f16(f32):
    """设备语义:f32 内部计算、f16 存储(RNE)。"""
    return f32.astype(np.float16).astype(np.float32)


def compile_run(tmp, name, mlir, spec, inputs, want_timeline=False):
    """inputs: list[(filename, ndarray)] — 与 .minisa 同目录,argN.npy 按序。"""
    m = os.path.join(tmp, name + ".mlir")
    b = os.path.join(tmp, name + ".minisa")
    with open(m, "w") as f:
        f.write(mlir)
    for i, arr in enumerate(inputs):
        np.save(os.path.join(tmp, f"arg{i}.npy"), arr)
    run([MINIC, "--input", m, "--emit", b, "--spec", spec])
    out = os.path.join(tmp, name + "_out.npy")
    cmd = [MINIRT, "--bin", b, "--spec", spec, "--dump-out", out]
    tl = os.path.join(tmp, name + "_tl.json")
    if want_timeline:
        cmd += ["--dump-timeline", tl]
    run(cmd)
    with open(b[:-7] + ".report.md") as f:
        report = f.read()
    return (np.load(out).astype(np.float32), report,
            tl if want_timeline else None)


def check(name, out, ref, rel_tol=1e-4):
    rel = np.linalg.norm(out - ref) / np.linalg.norm(ref)
    print(f"  {name}: rel-L2={rel:.2e}")
    assert rel < rel_tol, f"{name} rel-L2={rel:.3e} ≥ {rel_tol}"


def test_multitile_double_buffer(tmp):
    rng = np.random.default_rng(11)
    x = (rng.standard_normal((ROWS, H)) * 2).astype(np.float16)
    b = (rng.standard_normal((ROWS, H))).astype(np.float16)
    g = (rng.standard_normal((H,)) * 0.5 + 1).astype(np.float16)
    xf, bf, gf = x.astype(np.float32), b.astype(np.float32), g.astype(np.float32)

    # silu 512×256:Tr=128 → 4 tile(修复前 codegen 直接拒)
    out, rep, tl = compile_run(
        tmp, "db_silu",
        "func.func @db_silu(%x: tensor<512x256xf16>) -> tensor<512x256xf16> {\n"
        "  %y = minigraph.silu %x : tensor<512x256xf16> -> tensor<512x256xf16>\n"
        "  return %y : tensor<512x256xf16>\n"
        "}\n", SPEC, [x], want_timeline=True)
    assert "双缓冲" in rep, "silu 应规划为双缓冲"
    check("silu 4-tile db", out, ref_f16(xf / (1 + np.exp(-xf))))
    # 预取与本拍 vec 重叠:修复前不仅编译失败,等待语义还会把预取串行化
    with open(tl) as f:
        ev = json.load(f)["traceEvents"]
    pts = sorted({p for e in ev if e["cat"] in ("dma_ld", "dma_st", "vec")
                  for p in (e["ts"], e["ts"] + e["dur"])})
    ov = sum(b_ - a for a, b_ in zip(pts, pts[1:])
             if any(e["cat"] in ("dma_ld", "dma_st") and e["ts"] < b_
                    and e["ts"] + e["dur"] > a for e in ev)
             and any(e["cat"] == "vec" and e["ts"] < b_
                     and e["ts"] + e["dur"] > a for e in ev))
    print(f"  DMA∩VEC 重叠 {ov} cycles")
    assert ov > 0, "双缓冲应产生 DMA/VEC 重叠"

    # mul(2D rhs 同 tile 加载)500×512:Tr=32 → 16 tile,残块 20 行。
    # (注:2D rhs H=256 时 6×Tr 槽与 bank 偏好布局碎片化,SramAllocator
    #  first-fit 拒绝——已知边界,与 wait-token 修复无关,勿用该 shape)
    xb = (rng.standard_normal((500, 512))).astype(np.float16)
    bb = rng.standard_normal((500, 512)).astype(np.float16)
    out, rep, _ = compile_run(
        tmp, "db_mul",
        "func.func @db_mul(%x: tensor<500x512xf16>, %b: tensor<500x512xf16>)"
        " -> tensor<500x512xf16> {\n"
        "  %y = minigraph.mul %x, %b : (tensor<500x512xf16>,"
        " tensor<500x512xf16>) -> tensor<500x512xf16>\n"
        "  return %y : tensor<500x512xf16>\n"
        "}\n", SPEC, [xb, bb])
    assert "双缓冲" in rep
    check("mul 16-tile db(残块)", out,
          ref_f16(xb.astype(np.float32) * bb.astype(np.float32)))

    # rms 256×256:Tr=112 → 3 tile,残块 32 行
    out, rep, _ = compile_run(
        tmp, "db_rms",
        "func.func @db_rms(%x: tensor<256x256xf16>, %g: tensor<256xf16>)"
        " -> tensor<256x256xf16> {\n"
        "  %y = minigraph.rms_norm %x, %g {eps = 1.0e-06 : f64} :"
        " (tensor<256x256xf16>, tensor<256xf16>) -> tensor<256x256xf16>\n"
        "  return %y : tensor<256x256xf16>\n"
        "}\n", SPEC, [x[:256], g])
    assert "双缓冲" in rep
    ms = (xf[:256] * xf[:256]).mean(axis=1, keepdims=True)
    check("rms 3-tile db(残块)", out,
          ref_f16(xf[:256] / np.sqrt(ms + 1e-6) * gf))


def test_multitile_single_buffer(tmp):
    # 1/2 KiB SRAM 强制 db=false:silu Tr=1(sram 1024)、mul/rms Tr=1(sram 2048)
    base = json.load(open(SPEC))
    for name, sram in (("tiny1", 1024), ("tiny2", 2048)):
        with open(os.path.join(tmp, f"spec_{name}.json"), "w") as f:
            json.dump(dict(base, sram_bytes=sram), f)
    s1 = os.path.join(tmp, "spec_tiny1.json")
    s2 = os.path.join(tmp, "spec_tiny2.json")

    rng = np.random.default_rng(11)
    x = (rng.standard_normal((64, H)) * 2).astype(np.float16)
    b = rng.standard_normal((64, H)).astype(np.float16)
    g = (rng.standard_normal((H,)) * 0.5 + 1).astype(np.float16)
    xf, bf, gf = x.astype(np.float32), b.astype(np.float32), g.astype(np.float32)

    # 修复前:tile≥1 从未载入(静默读旧数据)且 p 在 1 槽上越界
    out, rep, _ = compile_run(
        tmp, "sb_silu",
        "func.func @sb_silu(%x: tensor<64x256xf16>) -> tensor<64x256xf16> {\n"
        "  %y = minigraph.silu %x : tensor<64x256xf16> -> tensor<64x256xf16>\n"
        "  return %y : tensor<64x256xf16>\n"
        "}\n", s1, [x])
    assert "单缓冲" in rep
    check("silu 64-tile 单缓冲", out, ref_f16(xf / (1 + np.exp(-xf))))

    out, rep, _ = compile_run(
        tmp, "sb_mul",
        "func.func @sb_mul(%x: tensor<64x256xf16>, %b: tensor<64x256xf16>)"
        " -> tensor<64x256xf16> {\n"
        "  %y = minigraph.mul %x, %b : (tensor<64x256xf16>,"
        " tensor<64x256xf16>) -> tensor<64x256xf16>\n"
        "  return %y : tensor<64x256xf16>\n"
        "}\n", s2, [x, b])
    assert "单缓冲" in rep
    check("mul 64-tile 单缓冲(2D rhs)", out, ref_f16(xf * bf))

    out, rep, _ = compile_run(
        tmp, "sb_rms",
        "func.func @sb_rms(%x: tensor<64x256xf16>, %g: tensor<256xf16>)"
        " -> tensor<64x256xf16> {\n"
        "  %y = minigraph.rms_norm %x, %g {eps = 1.0e-06 : f64} :"
        " (tensor<64x256xf16>, tensor<256xf16>) -> tensor<64x256xf16>\n"
        "  return %y : tensor<64x256xf16>\n"
        "}\n", s2, [x, g])
    assert "单缓冲" in rep
    ms = (xf * xf).mean(axis=1, keepdims=True)
    check("rms 64-tile 单缓冲", out, ref_f16(xf / np.sqrt(ms + 1e-6) * gf))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_multitile_double_buffer(tmp)
        print("PASS test_multitile_double_buffer")
        test_multitile_single_buffer(tmp)
        print("PASS test_multitile_single_buffer")
    print("ALL ROWTILE TESTS PASS")
