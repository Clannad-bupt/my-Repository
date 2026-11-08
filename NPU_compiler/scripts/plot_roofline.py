#!/usr/bin/env python3
"""Roofline 分析(README §13 步骤 10、§2 验收口径)。

  python scripts/plot_roofline.py trace.json [--spec configs/npu256.json] [--out md]

输入:minirt --dump-timeline 产出的 chrome trace。事件自带 args:
  - DMA_LD/DMA_ST 事件 args.bytes = 实际 DMA 字节(双缓冲重复加载计入,§13 步骤 10)
  - GEMM 事件 args.flops = 2*M*N*K
  - HOST_CPY_* 事件 args.bytes = host link 流量(单列,不计入 DRAM 口径)

输出(文本报告,stdout;--out 可另存 markdown):
  - 总算术强度 AI = FLOPs / DRAM_bytes
  - 实测性能 perf = FLOPs / total_cycles(FLOP/cycle 与 TFLOPS)
  - 带宽屋顶 / 计算屋顶 / 脊点,以及「为什么没打满峰值」的分解
  - 每个引擎的占用率(busy/total)与 DMA↔GEMM 重叠量

matplotlib 可用时另存 roofline.png;无 matplotlib 时只出文本(不影响验收)。
"""
import argparse
import json
import os
import sys

_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_DEFAULT_SPEC = os.path.join(_REPO, "configs", "npu256.json")


def load_spec(path):
    with open(path) as f:
        return json.load(f)


def analyze(trace_path, spec):
    with open(trace_path) as f:
        events = json.load(f)["traceEvents"]

    dev = [e for e in events if e.get("cat") != "host"]
    if not dev:
        raise SystemExit(f"{trace_path}: 无设备侧事件")

    total_cycles = max(e["ts"] + e["dur"] for e in events) - \
        min(e["ts"] for e in events)

    flops = sum(e.get("args", {}).get("flops", 0) for e in dev)
    dma_bytes = sum(e.get("args", {}).get("bytes", 0)
                    for e in dev if e["cat"] in ("dma_ld", "dma_st"))
    host_bytes = sum(e.get("args", {}).get("bytes", 0)
                     for e in events if e.get("cat") == "host")

    busy = {}
    for e in dev:
        busy[e["cat"]] = busy.get(e["cat"], 0) + e["dur"]
    gemm_busy = busy.get("gemm", 0)
    dma_busy = busy.get("dma_ld", 0) + busy.get("dma_st", 0)
    # DMA ∪ GEMM 的覆盖区间(逐点求并)
    points = sorted({p for e in dev if e["cat"] in ("dma_ld", "dma_st", "gemm")
                     for p in (e["ts"], e["ts"] + e["dur"])})
    union = overlap = 0
    for a, b in zip(points, points[1:]):
        dma_on = any(e["cat"] in ("dma_ld", "dma_st") and e["ts"] < b and e["ts"] + e["dur"] > a
                     for e in dev)
        gemm_on = any(e["cat"] == "gemm" and e["ts"] < b and e["ts"] + e["dur"] > a
                      for e in dev)
        if dma_on or gemm_on:
            union += b - a
        if dma_on and gemm_on:
            overlap += b - a

    freq = spec["freq_hz"]
    peak_macs_f16 = spec["macs_per_cycle"]
    peak_macs_i8 = spec.get("macs_per_cycle_int8", peak_macs_f16)
    peak_macs_f8 = spec.get("macs_per_cycle_fp8", peak_macs_f16)  # 三期(§6.6)
    peak_flop_per_cycle = 2.0 * peak_macs_f16  # 1 MAC = 2 FLOP(f16 屋顶口径)
    dma_bytes_per_cycle = spec["dram_bw_GBps"] * 1e9 / freq
    ridge_ai = peak_flop_per_cycle / dma_bytes_per_cycle  # FLOP/byte

    ai = flops / dma_bytes if dma_bytes else float("inf")
    perf_per_cycle = flops / total_cycles if total_cycles else 0.0
    perf_tflops = perf_per_cycle * freq / 1e12
    peak_tflops = peak_flop_per_cycle * freq / 1e12
    # 混合工作负载计算屋顶:逐 GEMM 事件按自身峰值折算理想周期再求和
    # (GEMM_I8 → macs_per_cycle_int8,GEMM_F8 → macs_per_cycle_fp8,
    # 普通 GEMM → macs_per_cycle),避免 int8/fp8 trace 被错算成 f16 屋顶
    # (二期-INT8 / 三期-FP8)
    gemm_ev = [e for e in dev if e["cat"] == "gemm"
               and e.get("args", {}).get("flops", 0)]
    peak_of = {"GEMM_I8": peak_macs_i8, "GEMM_F8": peak_macs_f8}
    roof_cycles = sum(
        e["args"]["flops"] / (2.0 * peak_of.get(e["name"], peak_macs_f16))
        for e in gemm_ev)
    compute_roof_tflops = (flops / roof_cycles) * freq / 1e12 \
        if roof_cycles else peak_tflops
    bw_roof_tflops = ai * dma_bytes_per_cycle * freq / 1e12
    roofline_cap = min(compute_roof_tflops, bw_roof_tflops)

    return {
        "trace": trace_path,
        "flops": flops, "dma_bytes": dma_bytes, "host_bytes": host_bytes,
        "total_cycles": total_cycles, "busy": busy,
        "gemm_busy": gemm_busy, "dma_busy": dma_busy, "overlap": overlap,
        "ai": ai, "ridge_ai": ridge_ai,
        "perf_per_cycle": perf_per_cycle, "perf_tflops": perf_tflops,
        "peak_tflops": peak_tflops, "bw_roof_tflops": bw_roof_tflops,
        "compute_roof_tflops": compute_roof_tflops,
        "has_i8": any(e["name"] == "GEMM_I8" for e in dev),
        "has_f8": any(e["name"] == "GEMM_F8" for e in dev),
        "roofline_cap": roofline_cap,
        "freq": freq, "peak_flop_per_cycle": peak_flop_per_cycle,
        "dma_bytes_per_cycle": dma_bytes_per_cycle,
    }


def render(r, spec):
    pct = lambda x: f"{100.0 * x:.1f}%"
    lines = [
        "# Roofline 报告(§13 步骤 10)",
        "",
        f"- trace: `{r['trace']}`",
        f"- spec: macs/cycle={spec['macs_per_cycle']} "
        f"(峰值 {r['peak_tflops']:.3f} TFLOPS @ {r['freq'] / 1e9:.1f} GHz), "
        f"DRAM 带宽 {spec['dram_bw_GBps']} GB/s ({r['dma_bytes_per_cycle']:.1f} B/cycle)",
        "",
        "## 实测口径(全部来自 trace 事件 args,非理论体积)",
        "",
        f"- GEMM FLOPs(Σ 2·M·N·K): **{r['flops']:,}**",
        f"- 实际 DMA 字节(LD+ST,含双缓冲重复加载): **{r['dma_bytes']:,}**",
        f"- host link 字节(HOST_CPY,单列不计入 AI): {r['host_bytes']:,}",
        f"- 总周期: {r['total_cycles']:,}",
        "",
        "## Roofline",
        "",
        f"- 算术强度 AI = FLOPs/DRAM_bytes = **{r['ai']:.2f} FLOP/B** "
        f"(脊点 {r['ridge_ai']:.2f})",
        f"- 实测性能 = FLOPs/cycles = {r['perf_per_cycle']:.2f} FLOP/cycle "
        f"= **{r['perf_tflops']:.4f} TFLOPS**",
        f"- 计算屋顶 {r['compute_roof_tflops']:.3f} TFLOPS"
        + ("(含 GEMM_I8,按 i8 峰值折算)" if r["has_i8"] else "")
        + ("(含 GEMM_F8,按 fp8 峰值折算)" if r["has_f8"] else "")
        + f" | 带宽屋顶(AI×BW) "
        f"{r['bw_roof_tflops']:.4f} TFLOPS | 取低 {r['roofline_cap']:.4f}",
        f"- 实测 / roofline 上限: {pct(r['perf_tflops'] / r['roofline_cap'])}"
        if r["roofline_cap"] else "- n/a",
        "",
        "## 引擎占用与重叠",
        "",
        f"- gemm busy {r['busy'].get('gemm', 0):,} cycles "
        f"({pct(r['busy'].get('gemm', 0) / r['total_cycles'])} of total)",
        f"- dma  busy {r['dma_busy']:,} cycles "
        f"({pct(r['dma_busy'] / r['total_cycles'])})",
        f"- vec  busy {r['busy'].get('vec', 0):,} cycles "
        f"({pct(r['busy'].get('vec', 0) / r['total_cycles'])})",
        f"- DMA∩GEMM 重叠 {r['overlap']:,} cycles(双缓冲收益)",
        "",
        "## 为什么没打满峰值",
        "",
    ]
    # 自动归因:占比最大的损失源
    reasons = []
    if r["ai"] < r["ridge_ai"]:
        reasons.append(
            f"AI={r['ai']:.2f} 低于脊点 {r['ridge_ai']:.2f},**带宽受限**;"
            "tile 越大 AI 越高,但受 SRAM 容量约束")
    else:
        reasons.append(
            f"AI={r['ai']:.2f} 高于脊点 {r['ridge_ai']:.2f},理论计算受限")
    gemm_gap = 1.0 - r["busy"].get("gemm", 0) / r["total_cycles"]
    if gemm_gap > 0.02:
        reasons.append(
            f"GEMM 引擎空闲 {pct(gemm_gap)}:DMA 延迟(每次传输 "
            "+latency cycles)与算子间 DRAM 物化的 round-trip 无法被"
            "双缓冲完全掩盖(第一期预期现象,§13 步骤 10 口径说明)")
    if r["overlap"] == 0:
        reasons.append("无 DMA/GEMM 重叠:检查 BAR 粒度和预取是否生效")
    else:
        reasons.append(
            f"DMA/GEMM 已重叠 {r['overlap']:,} cycles,剩余暴露为"
            " k-loop 首尾气泡与 DMA latency")
    lines += [f"{i + 1}. {t}" for i, t in enumerate(reasons)]
    return "\n".join(lines) + "\n"


def try_plot(r, out_png):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return False
    import numpy as np
    ais = np.logspace(-1, 3, 100)
    peak = r["peak_tflops"]
    bw = r["dma_bytes_per_cycle"] * r["freq"] / 1e12
    roof = np.minimum(peak, ais * bw)
    plt.figure(figsize=(6, 4))
    plt.loglog(ais, roof, "k-", label="roofline")
    plt.axvline(r["ridge_ai"], color="gray", ls=":", label=f"ridge AI={r['ridge_ai']:.1f}")
    plt.plot([r["ai"]], [r["perf_tflops"]], "ro",
             label=f"实测 ({r['ai']:.2f}, {r['perf_tflops']:.4f})")
    plt.xlabel("AI (FLOP/byte)")
    plt.ylabel("TFLOPS")
    plt.legend()
    plt.tight_layout()
    plt.savefig(out_png)
    return True


def main():
    ap = argparse.ArgumentParser(description="MiniNPU roofline 分析(§13 步骤 10)")
    ap.add_argument("trace", help="minirt --dump-timeline 产出的 trace.json")
    ap.add_argument("--spec", default=_DEFAULT_SPEC)
    ap.add_argument("--out", help="markdown 报告输出路径(默认只打 stdout)")
    ap.add_argument("--png", help="roofline 图输出路径(需 matplotlib)")
    args = ap.parse_args()

    spec = load_spec(args.spec)
    r = analyze(args.trace, spec)
    text = render(r, spec)
    sys.stdout.write(text)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
        print(f"[roofline] wrote {args.out}", file=sys.stderr)
    png = args.png or (args.out[:-3] + ".png" if args.out and
                       args.out.endswith(".md") else None)
    if png:
        if try_plot(r, png):
            print(f"[roofline] wrote {png}", file=sys.stderr)
        else:
            print("[roofline] matplotlib 不可用,跳过 PNG(文本报告不受影响)",
                  file=sys.stderr)


if __name__ == "__main__":
    main()
