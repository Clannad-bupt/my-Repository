# fx_to_minigraph:torch.export 的 FX 图 → minigraph MLIR 文本(README §7.2/§7.3)。
#
# 支持白名单(第一期):
#   aten.matmul / aten.mm          → minigraph.matmul
#   aten.addmm(bias 1-D)           → minigraph.matmul + bias
#   aten.linear(x, w[, bias])      → minigraph.matmul transpose_b (+bias)
#   aten.silu / aten.mul / aten.add(tensor,tensor)
#   aten.transpose / aten.t        → minigraph.transpose(由中端 fold 进 matmul)
#   aten.view / aten.reshape       → minigraph.reshape;aten.contiguous/clone → 别名
#   RMSNorm 6 节点模板(§7.3):
#     pow(x,2) → mean(-1,keepdim) → add(eps) → rsqrt → mul(x) → mul(gamma)
#   匹配成功整段替换为 minigraph.rms_norm;匹配失败的 pow/mean/rsqrt 等
#   视为 unsupported(第一期不允许逐算子 lowering 这些,避免错误融合)。
#
# 数值约定:第一期 FP16 —— 输入与权重 npy 一律按 f16 落盘,MLIR 张量类型 f16。
#
# 两个层次:
#   emit_subgraph(nodes, inputs, outputs, name) → MLIR 文本(供 partition 用)
#   export_to_minigraph(ep, out_dir, name)      → 整图直译 + manifest 落盘
import json
import os
import re

import numpy as np
import torch

from torch.ops import aten


class UnsupportedError(Exception):
    """FX 节点不在白名单 / 形状不满足第一期约束(§7.2:触发 partition/fallback)。"""


def _target_str(node):
    return str(node.target)


def _is(node, target):
    return node.op == "call_function" and node.target == target


def _shape_of(node):
    """node.meta['val'] → tuple shape(FakeTensor/真实 tensor 均可)。"""
    val = node.meta.get("val")
    if val is None:
        raise UnsupportedError(f"节点 {node.name} 缺少 meta['val'](需 torch.export 产物)")
    if isinstance(val, (tuple, list)):  # 多输出算子第一期不支持
        raise UnsupportedError(f"节点 {node.name} 多输出,第一期不支持")
    return tuple(int(d) for d in val.shape)


def _fmt_float(v):
    """MLIR 浮点字面量必须含小数点(1e-06 → 1.0e-06)。"""
    s = repr(float(v))
    if "." not in s and "e" in s:
        s = s.replace("e", ".0e", 1)
    return s


def match_rms_norm(nodes):
    """§7.3 固定 6 节点模板。返回 (covered, fused):
    covered: set(node) —— 模板内部节点,发射时跳过;
    fused:   dict node(gamma-mul) -> (x_node, gamma_node, eps)。
    """
    covered, fused = set(), {}
    for n in nodes:
        if not _is(n, aten.mul.Tensor):
            continue
        a, b = n.args
        for mul1, gamma in ((a, b), (b, a)):
            if not (getattr(gamma, "op", None) == "placeholder"):
                continue
            if not _is(mul1, aten.mul.Tensor):
                continue
            c, d = mul1.args
            if _is(d, aten.rsqrt.default):
                x, rsqrt = c, d
            elif _is(c, aten.rsqrt.default):
                x, rsqrt = d, c
            else:
                continue
            add = rsqrt.args[0]
            if not _is(add, aten.add.Tensor):
                continue
            mean, eps = add.args
            if not isinstance(eps, (int, float)) or not _is(mean, aten.mean.dim):
                continue
            pow_node, dims, keepdim = mean.args
            if not _is(pow_node, aten.pow.Tensor_Scalar):
                continue
            base, exponent = pow_node.args
            if exponent != 2 or base is not x:
                continue
            if list(dims) not in ([-1], [1]) or keepdim is not True:
                continue
            covered.update([pow_node, mean, add, rsqrt, mul1])
            fused[n] = (x, gamma, float(eps))
            break
    return covered, fused


def _ty(shape, dtype):
    return "tensor<" + "x".join(str(d) for d in shape) + "x" + dtype + ">"


def _emit_node(n, fused, dtype):
    """单个 call_function 节点 → MLIR 行(不含 rms 覆盖节点的跳过逻辑,调用方保证)。"""
    out_shape = _shape_of(n)
    if not (1 <= len(out_shape) <= 2):
        raise UnsupportedError(f"节点 {n.name} rank={len(out_shape)},第一期只支持 1/2-D")
    out_ty = _ty(out_shape, dtype)
    ssa = f"%{n.name}"

    def ty1(x):
        return _ty(_shape_of(x), dtype)

    if n in fused:
        x, gamma, eps = fused[n]
        return (
            f"  {ssa} = minigraph.rms_norm %{x.name}, %{gamma.name} "
            f"{{eps = {_fmt_float(eps)} : f64}}\n"
            f"       : ({ty1(x)}, {ty1(gamma)}) -> {out_ty}", None)
    if _is(n, aten.matmul.default) or _is(n, aten.mm.default):
        a, b = n.args
        return (f"  {ssa} = minigraph.matmul %{a.name}, %{b.name}\n"
                f"       : ({ty1(a)}, {ty1(b)}) -> {out_ty}", None)
    if _is(n, aten.addmm.default):
        bias, a, b = n.args
        if len(_shape_of(bias)) != 1 or _shape_of(bias)[0] != out_shape[-1]:
            raise UnsupportedError(f"addmm 的 bias 必须是 1-D 长度 N({n.name})")
        return (f"  {ssa} = minigraph.matmul %{a.name}, %{b.name}, %{bias.name}\n"
                f"       : ({ty1(a)}, {ty1(b)}, {ty1(bias)}) -> {out_ty}", None)
    if _is(n, aten.linear.default):
        x, w = n.args[0], n.args[1]
        bias = n.args[2] if len(n.args) > 2 else None
        tb = " {transpose_b = true}"
        if bias is None:
            return (f"  {ssa} = minigraph.matmul %{x.name}, %{w.name}{tb}\n"
                    f"       : ({ty1(x)}, {ty1(w)}) -> {out_ty}", None)
        if len(_shape_of(bias)) != 1:
            raise UnsupportedError(f"linear 的 bias 必须是 1-D({n.name})")
        return (f"  {ssa} = minigraph.matmul %{x.name}, %{w.name}, %{bias.name}{tb}\n"
                f"       : ({ty1(x)}, {ty1(w)}, {ty1(bias)}) -> {out_ty}", None)
    if _is(n, aten.silu.default):
        (x,) = n.args
        return (f"  {ssa} = minigraph.silu %{x.name} : {ty1(x)} -> {out_ty}", None)
    if _is(n, aten.mul.Tensor):
        a, b = n.args
        if not isinstance(a, torch.fx.Node) or not isinstance(b, torch.fx.Node):
            raise UnsupportedError(f"标量 mul 暂不支持({n.name})")
        return (f"  {ssa} = minigraph.mul %{a.name}, %{b.name}\n"
                f"       : ({ty1(a)}, {ty1(b)}) -> {out_ty}", None)
    if _is(n, aten.add.Tensor):
        a, b = n.args
        if not isinstance(a, torch.fx.Node) or not isinstance(b, torch.fx.Node):
            raise UnsupportedError(f"标量 add 暂不支持({n.name};RMSNorm 外的)")
        return (f"  {ssa} = minigraph.add %{a.name}, %{b.name}\n"
                f"       : ({ty1(a)}, {ty1(b)}) -> {out_ty}", None)
    if _is(n, aten.transpose.int):
        x, d0, d1 = n.args
        shape = _shape_of(x)
        perm = list(range(len(shape)))
        perm[d0], perm[d1] = perm[d1], perm[d0]
        perm_s = "[" + ", ".join(str(p) for p in perm) + "]"
        return (f"  {ssa} = minigraph.transpose %{x.name} {{permutation = {perm_s} : i64}}\n"
                f"       : {ty1(x)} -> {out_ty}", None)
    if _is(n, aten.t.default):
        x = n.args[0]
        return (f"  {ssa} = minigraph.transpose %{x.name} {{permutation = [1, 0] : i64}}\n"
                f"       : {ty1(x)} -> {out_ty}", None)
    if _is(n, aten.view.default) or _is(n, aten.reshape.default):
        x = n.args[0]
        shape_s = "[" + ", ".join(str(d) for d in out_shape) + "]"
        return (f"  {ssa} = minigraph.reshape %{x.name} {{shape = {shape_s} : i64}}\n"
                f"       : {ty1(x)} -> {out_ty}", None)
    if _is(n, aten.contiguous.default) or _is(n, aten.clone.default):
        # 第一期:视为别名(输入已按行主序落盘)
        return (None, n.args[0].name)
    raise UnsupportedError(f"{_target_str(n)} 不在白名单(§7.2)")


def emit_subgraph(nodes, inputs, outputs, name, dtype="f16"):
    """发射一个(子)图为 func.func。

    nodes:   拓扑序的 call_function 节点(可含 rms 模板内部节点,自动跳过);
    inputs:  边界输入节点序(placeholder 或其它分量的产出),即函数参数顺序;
    outputs: 分量输出节点序(≥1)。
    返回 MLIR 文本。
    """
    if dtype != "f16":
        raise UnsupportedError("第一期只做 FP16(§2.3)")
    covered, fused = match_rms_norm(nodes)

    sig = ", ".join(f"%{n.name}: {_ty(_shape_of(n), dtype)}" for n in inputs)
    body, alias = [], {}
    for n in nodes:
        if n in covered:
            continue
        line, alias_to = _emit_node(n, fused, dtype)
        if alias_to is not None:
            # 链式别名(contiguous(clone(x)))归约到非别名真身;
            # 节点按拓扑序处理,alias_to 若仍是别名则必已登记
            alias[n.name] = alias.get(alias_to, alias_to)
            continue
        body.append(line)

    def ssa_of(node):
        return alias.get(node.name, node.name)

    out_tys_list = [_ty(_shape_of(o), dtype) for o in outputs]
    ret_vals = ", ".join(f"%{ssa_of(o)}" for o in outputs)
    body.append(f"  return {ret_vals} : {', '.join(out_tys_list)}")
    func_ret_ty = out_tys_list[0] if len(outputs) == 1 else "(" + ", ".join(out_tys_list) + ")"

    # 别名回填:被别名节点的引用改成真身。
    # 词边界正则:不止 `,` `)` ` ` 三种后随字符,行尾(matmul/add/mul 的
    # 最后一个操作数后面是换行)也必须命中——旧实现漏掉行尾,别名喂到该
    # 位置时会留下引用不存在 SSA 名的非法 MLIR。
    text_body = "\n".join(body)
    for old, new in alias.items():
        text_body = re.sub(r"%" + re.escape(old) + r"(?![A-Za-z0-9_.])",
                           "%" + new, text_body)
    return (f"// torch_bridge 自动生成(§7):torch.export FX → minigraph\n"
            f"func.func @{name}({sig}) -> {func_ret_ty} {{\n{text_body}\n}}\n")


def export_to_minigraph(ep, out_dir, name="mlp", dtype="f16"):
    """导出一个 ExportedProgram → (mlir_path, manifest_path)。

    参数顺序 = FX placeholder 顺序;manifest 记录每个 arg 是输入还是权重。
    """
    gm = ep.graph_module
    sig = ep.graph_signature
    nodes = list(gm.graph.nodes)
    placeholders = [n for n in nodes if n.op == "placeholder"]
    param_names = set(sig.parameters) | set(sig.buffers)

    # placeholder target 形如 'p_w_gate';graph_signature 里参数名不带 'p_' 前缀
    def classify(n):
        t = n.target
        bare = t[2:] if t.startswith("p_") else t
        if t in param_names or bare in param_names:
            return "weight", bare
        return "input", t

    data_dir = os.path.join(out_dir, f"{name}_data")
    os.makedirs(data_dir, exist_ok=True)
    state = dict(ep.state_dict)  # FQN('norm_gamma' 等)→ tensor

    manifest = {"inputs": [], "weights": []}
    for i, n in enumerate(placeholders):
        kind, bare = classify(n)
        npy = f"{name}_data/{bare}.npy"
        if kind == "weight":
            arr = state[bare].detach().cpu().numpy().astype(np.float16)
            np.save(os.path.join(out_dir, npy), arr)
            manifest["weights"].append({"arg": i, "name": bare, "npy": npy})
        else:
            manifest["inputs"].append({"arg": i, "name": bare, "npy": npy})

    out_node = next(n for n in nodes if n.op == "output")
    outputs = [o for o in out_node.args[0] if o is not None]
    call_nodes = [n for n in nodes if n.op == "call_function"]
    text = emit_subgraph(call_nodes, placeholders, outputs, name, dtype)

    mlir_path = os.path.join(out_dir, f"{name}.minigraph.mlir")
    with open(mlir_path, "w") as f:
        f.write(text)
    manifest_path = os.path.join(out_dir, f"{name}.weights.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=1)
    return mlir_path, manifest_path
