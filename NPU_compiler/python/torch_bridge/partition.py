# partition:FX 图染色 + 连通块切分 + 逐分量编译 + 解释执行(README §13 步骤 9、§7.4)。
#
#   cg = compile_graph(ep, work_dir, name="mlp", spec="configs/npu256.json")
#   y = cg(x32)                       # supported 分量走 NPU,其余 torch eager
#
#   torch.compile 最小 backend(§7.4 加分项):
#   opt_model = torch.compile(model, backend=mininpu_backend("/tmp/tc"))
#
# 规则(§8.2/步骤 9):
#   - 白名单(§7.2)且形状满足第一期约束(1/2-D)的节点标 supported;
#     RMSNorm 模板内部节点仅当整段匹配成功时 supported(否则整段落 CPU);
#   - 只沿 data edge 合并同色 supported 节点 → 最大连通块,每块一个 minisa;
#   - block 间按原图拓扑执行;边界拷贝 = torch tensor ↔ npy(f16),即 memcpy HtoD;
#   - 分量 placeholder 输入若是 parameter,编译期进常量段(manifest weights);
#     其余边界输入为 runtime input。
import json
import os
import subprocess

import numpy as np
import torch

from torch.ops import aten

from .fx_to_minigraph import (UnsupportedError, _is, _shape_of, emit_subgraph,
                              match_rms_norm)

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
_DEFAULT_MINIC = os.path.join(_REPO, "build", "minic")

# 白名单(§7.2;pow/mean/add-scalar/rsqrt 只能活在匹配成功的 RMSNorm 模板里)
_WHITELIST = (aten.matmul.default, aten.mm.default, aten.addmm.default,
              aten.linear.default, aten.silu.default, aten.mul.Tensor,
              aten.add.Tensor, aten.transpose.int,
              aten.t.default, aten.view.default, aten.reshape.default,
              aten.contiguous.default, aten.clone.default)


def _is_supported_node(n, rms_covered, rms_fused):
    if n.op != "call_function":
        return False
    if n in rms_covered or n in rms_fused:
        return True
    if n.target not in _WHITELIST:
        return False
    # 标量变体(aten.add/mul 第二参数为标量)只认 RMSNorm 模板里的那个 add
    if n.target in (aten.mul.Tensor, aten.add.Tensor):
        a, b = n.args
        if not isinstance(a, torch.fx.Node) or not isinstance(b, torch.fx.Node):
            return False
    # 第一期约束:1/2-D(lowering 只覆盖 matmul2D / [rows,H] / 同形 eltwise)
    try:
        if not (1 <= len(_shape_of(n)) <= 2):
            return False
        for a in n.args:
            if isinstance(a, torch.fx.Node) and len(_shape_of(a)) > 2:
                return False
    except UnsupportedError:
        return False
    return True


class Component:
    """一个 supported 连通块 = 一个 minisa。"""

    def __init__(self, idx, nodes):
        self.idx = idx
        self.nodes = nodes           # 拓扑序 call_function 节点
        self.inputs = []             # 边界输入节点(参数序)
        self.outputs = []            # 分量输出节点
        self.weight_args = {}        # 输入节点 → (arg index, bare name)
        self.bin = None              # 编译产物路径
        self.name = f"comp_{idx}"


def _value(a, env):
    if isinstance(a, torch.fx.Node):
        return env[a]
    if isinstance(a, dict):
        return {k: _value(v, env) for k, v in a.items()}
    if isinstance(a, (list, tuple)):
        return type(a)(_value(x, env) for x in a)
    return a


class CompiledGraph:
    """逐节点解释执行:supported 分量一次 NPU launch,unsupported 节点 torch eager。"""

    def __init__(self, ep, work_dir, name="graph", spec=None, minic=None,
                 minirt=None):
        import mininpu_rt  # python/mininpu_rt(与 torch_bridge 同级)
        self._rt = mininpu_rt
        self.spec = spec
        self.minirt = minirt
        self.last_npu_launches = 0  # 验收用:上一次 __call__ 的 NPU launch 数

        gm = ep.graph_module
        sig = ep.graph_signature
        nodes = list(gm.graph.nodes)
        self._param_names = set(sig.parameters) | set(sig.buffers)
        state = dict(ep.state_dict)

        covered, fused = match_rms_norm(nodes)
        supported = {n for n in nodes
                     if _is_supported_node(n, covered, fused)}
        unsupported = [n for n in nodes if n.op == "call_function"
                       and n not in supported]

        # 并查集:只沿 data edge 合并 supported 节点
        parent = {n: n for n in supported}

        def find(n):
            while parent[n] is not n:
                parent[n] = parent[parent[n]]
                n = parent[n]
            return n

        def union(a, b):
            ra, rb = find(a), find(b)
            if ra is not rb:
                parent[rb] = ra

        for n in nodes:
            if n not in supported:
                continue
            for a in n.args:
                if isinstance(a, torch.fx.Node) and a in supported:
                    union(n, a)

        groups = {}
        for n in nodes:
            if n in supported:
                groups.setdefault(find(n), []).append(n)

        # 编译每个连通块
        self.components = []
        minic = minic or _DEFAULT_MINIC
        for idx, comp_nodes in enumerate(groups.values()):
            comp = Component(idx, comp_nodes)
            node_set = set(comp_nodes)
            for n in comp_nodes:
                if n in fused:  # rms 的 x/gamma 也是边界
                    x, gamma, _ = fused[n]
                    for b in (x, gamma):
                        if b not in node_set and b not in comp.inputs:
                            comp.inputs.append(b)
                for a in n.args:
                    if isinstance(a, torch.fx.Node) and a not in node_set \
                            and a not in comp.inputs:
                        comp.inputs.append(a)
            for n in comp_nodes:
                ext_use = any(u not in node_set for u in n.users)
                if ext_use or any(n is o for o in self._graph_outputs(nodes)):
                    comp.outputs.append(n)

            mlir_text = emit_subgraph(comp_nodes, comp.inputs, comp.outputs,
                                      comp.name)
            data_dir = os.path.join(work_dir, f"{comp.name}_data")
            os.makedirs(data_dir, exist_ok=True)
            manifest = {"inputs": [], "weights": []}
            for arg, b in enumerate(comp.inputs):
                is_param = False
                bare = None
                if b.op == "placeholder":
                    t = b.target
                    bare = t[2:] if t.startswith("p_") else t
                    is_param = t in self._param_names or bare in self._param_names
                if is_param:
                    npy = f"{comp.name}_data/{bare}.npy"
                    arr = state[bare].detach().cpu().numpy().astype(np.float16)
                    np.save(os.path.join(work_dir, npy), arr)
                    manifest["weights"].append({"arg": arg, "name": bare,
                                                "npy": npy})
                    comp.weight_args[b] = arg
                else:
                    manifest["inputs"].append({
                        "arg": arg, "name": b.name,
                        "npy": f"{comp.name}_data/{b.name}.npy"})  # 运行期被替换

            mlir_path = os.path.join(work_dir, f"{comp.name}.minigraph.mlir")
            with open(mlir_path, "w") as f:
                f.write(mlir_text)
            manifest_path = os.path.join(work_dir, f"{comp.name}.weights.json")
            with open(manifest_path, "w") as f:
                json.dump(manifest, f, indent=1)
            comp.bin = os.path.join(work_dir, f"{comp.name}.minisa")
            cmd = [minic, "--input", mlir_path, "--emit", comp.bin]
            if spec:
                cmd += ["--spec", spec]
            cmd += ["--manifest", manifest_path]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0:
                raise RuntimeError(f"minic 编译分量 {comp.name} 失败:\n{r.stderr}")
            self.components.append(comp)

        # 节点 → 分量(发射时查)
        self._node_comp = {}
        for comp in self.components:
            for n in comp.nodes:
                self._node_comp[n] = comp
        self._nodes = nodes
        self._unsupported = unsupported
        # 图的 user-input placeholder(按 sig.input_specs 顺序)
        self._user_inputs = [n for n in nodes if n.op == "placeholder"
                             and not (n.target in self._param_names or
                                      (n.target[2:] if n.target.startswith("p_")
                                       else n.target) in self._param_names)]
        self._param_placeholders = [n for n in nodes if n.op == "placeholder"
                                    and n not in self._user_inputs]
        self._state = state

    @staticmethod
    def _graph_outputs(nodes):
        out = next((n for n in nodes if n.op == "output"), None)
        return [o for o in out.args[0] if o is not None] if out else []

    def __call__(self, *inputs):
        if len(inputs) != len(self._user_inputs):
            raise RuntimeError(f"期望 {len(self._user_inputs)} 个输入,"
                               f"收到 {len(inputs)}")
        env = {}
        for n, t in zip(self._user_inputs, inputs):
            env[n] = t
        for n in self._param_placeholders:
            t = n.target
            bare = t[2:] if t.startswith("p_") else t
            env[n] = self._state[bare]

        self.last_npu_launches = 0
        done_comps = set()
        for n in self._nodes:
            if n.op in ("placeholder", "output"):
                continue
            # 未进 supported 分量的一切节点(非白名单算子、call_method/getattr
            # 等 torch.export 规范化外形态)一律 host eager 兜底,而不是让
            # 整个图失败(§7.4:覆盖不了就落 CPU,覆盖率优雅退化)
            comp = self._node_comp.get(n)
            if comp is None:
                args = _value(n.args, env)
                kwargs = _value(n.kwargs, env)
                if n.op == "call_function":
                    env[n] = n.target(*args, **kwargs)
                elif n.op == "call_method":
                    env[n] = getattr(args[0], n.target)(*args[1:], **kwargs)
                elif n.op == "get_attr":
                    env[n] = getattr(args[0], n.target)
                else:
                    raise UnsupportedError(
                        f"FX node op={n.op} 无法 eager 兜底({n.name})")
                continue
            if comp in done_comps:
                continue
            done_comps.add(comp)
            feeds = {}
            for arg, b in enumerate(comp.inputs):
                if b in comp.weight_args:
                    continue  # 编译期已进常量段
                t = env[b]
                feeds[b.name] = t.detach().cpu().numpy().astype(np.float16)
            outs = self._rt.run(comp.bin, feeds, spec=self.spec,
                                minirt=self.minirt)
            self.last_npu_launches += 1
            for i, o in enumerate(comp.outputs):
                env[o] = torch.from_numpy(outs[f"out{i}"]).to(torch.float32)

        outs = self._graph_outputs(self._nodes)
        result = tuple(env[o] for o in outs)
        return result[0] if len(result) == 1 else result


def compile_graph(ep, work_dir, name="graph", spec=None, minic=None, minirt=None):
    """torch.export 产物 → 可调用 CompiledGraph。"""
    os.makedirs(work_dir, exist_ok=True)
    return CompiledGraph(ep, work_dir, name, spec, minic, minirt)


def mininpu_backend(work_dir, spec=None):
    """§7.4 最小 torch.compile backend(加分项)。

    dynamo 的 gm 先经 torch.export 再进 partition(参数随 export 提起,
    与主路径同一份代码)。compile_graph 返回的 callable 形状与
    backend 契约一致:fn(*flattened_inputs)。
    """
    counter = [0]

    def backend(gm, sample_inputs):
        ep = torch.export.export(gm, tuple(sample_inputs))
        counter[0] += 1
        cg = compile_graph(ep, work_dir, name=f"tc_{counter[0]}",
                           spec=spec)

        def fn(*inputs):
            # dynamo backend 契约:返回值结构对齐 gm output(总是 tuple);
            # 返回裸 tensor 会被 dynamo 当作多输出序列索引出行切片
            out = cg(*inputs)
            return out if isinstance(out, tuple) else (out,)

        return fn

    return backend
