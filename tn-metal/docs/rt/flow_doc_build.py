#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""flow_doc_build.py — LPU-FLOW.md.in + drawio 导出的 SVG → LPU-FLOW.md → HTML

流程:
  1. 在搜索路径下找文件名含 "lpu-flow" 的 .svg(按 4 个页面名模糊匹配)
  2. 把模板里的 <!--DIAGRAM:<页面名>--> 标记替换为 SVG 内容
     (去 XML 声明、压掉空行 —— pandoc 的 raw HTML 块遇空行会提前结束)
  3. 写出 LPU-FLOW.md, 再调 md2html.py 生成 LPU-FLOW.html

用法:
  python3 flow_doc_build.py [--svg-dir DIR ...] [--md2html PATH] [--no-html]

SVG 来源(rlespinasse/drawio-export 容器, 内含 drawio+xvfb):
  docker run --rm -v "$PWD":/data rlespinasse/drawio-export -f svg
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PAGES = ["1-host-runtime-UMD", "2-ap-sched-fw", "3-scalar-exec-fw",
         "4-qemu-xthead-decode"]
TEMPLATE = "LPU-FLOW.md.in"
OUT_MD = "LPU-FLOW.md"
DEFAULT_MD2HTML = "~/.claude/skills/md2html/md2html.py"


def norm(s):
    return "".join(ch.lower() for ch in s if ch.isalnum())


def strip_prolog(svg):
    return re.sub(r"^\s*(<\?xml[^>]*\?>\s*|<!DOCTYPE[^>]*>\s*)*", "", svg)


def find_svgs(roots):
    out = []
    for root in roots:
        for dirpath, _dirs, files in os.walk(root):
            for fn in files:
                if fn.lower().endswith(".svg") and "lpuflow" in norm(fn):
                    out.append(os.path.join(dirpath, fn))
    return sorted(set(out))


def main():
    ap = argparse.ArgumentParser(
        description="组装 LPU-FLOW.md 并调 md2html 出 HTML")
    ap.add_argument("--svg-dir", action="append", default=[],
                    help="SVG 搜索目录(默认本目录递归, 可多次)")
    ap.add_argument("--md2html", default=os.path.expanduser(DEFAULT_MD2HTML),
                    help="md2html.py 路径(默认 %s)" % DEFAULT_MD2HTML)
    ap.add_argument("--no-html", action="store_true",
                    help="只生成 md, 不跑 md2html")
    args = ap.parse_args()

    tpl_path = os.path.join(HERE, TEMPLATE)
    if not os.path.isfile(tpl_path):
        sys.exit("[flow_doc_build] 缺少模板: %s" % tpl_path)

    roots = [os.path.abspath(d) for d in (args.svg_dir or [HERE])]
    svgs = find_svgs(roots)
    if not svgs:
        sys.exit("[flow_doc_build] 在 %s 下没找到文件名含 LPU-FLOW 的 .svg\n"
                 "  先跑: docker run --rm -v \"$PWD\":/data "
                 "rlespinasse/drawio-export -f svg\n"
                 "  或用 --svg-dir 指定 SVG 所在目录" % roots)

    # 先按页面名匹配; 匹配不上的页面按文件名自然排序兜底(打印出来人工确认)
    assign, used = {}, set()
    for page in PAGES:
        key = norm(page)
        m = [f for f in svgs if key in norm(os.path.basename(f))
             and f not in used]
        if m:
            if len(m) > 1:
                print("[flow_doc_build] ⚠ 页面 %s 命中多个文件, 取排序第一个:"
                      % page)
                for f in m:
                    print("    %s" % f)
            assign[page] = m[0]
            used.add(m[0])
    rest = [f for f in svgs if f not in used]
    for page in PAGES:
        if page not in assign:
            if rest:
                assign[page] = rest.pop(0)
                print("[flow_doc_build] ⚠ 页面 %s 按排序兜底匹配(核对输出!)"
                      % page)
            else:
                sys.exit("[flow_doc_build] 页面 %s 没找到 SVG。找到的文件:\n  %s"
                         % (page, "\n  ".join(svgs)))
    for f in rest:
        print("[flow_doc_build] 忽略多余文件: %s" % f)

    with open(tpl_path, encoding="utf-8") as f:
        tpl = f.read()
    for page in PAGES:
        marker = "<!--DIAGRAM:%s-->" % page
        if marker not in tpl:
            sys.exit("[flow_doc_build] 模板缺标记 %s" % marker)
        with open(assign[page], encoding="utf-8") as f:
            svg = strip_prolog(f.read()).strip()
        svg = re.sub(r"\n[ \t]*\n+", "\n", svg)   # 压掉空行, 保住 raw HTML 块
        tpl = tpl.replace(marker, svg)
        print("[flow_doc_build] %s ← %s" % (page, assign[page]))

    out_md = os.path.join(HERE, OUT_MD)
    with open(out_md, "w", encoding="utf-8") as f:
        f.write(tpl)
    print("[flow_doc_build] 写出 %s" % out_md)

    if args.no_html:
        return
    if not os.path.isfile(args.md2html):
        sys.exit("[flow_doc_build] 找不到 md2html.py: %s (--md2html 指定)"
                 % args.md2html)
    r = subprocess.run([sys.executable, args.md2html, out_md])
    sys.exit(r.returncode)


if __name__ == "__main__":
    main()
