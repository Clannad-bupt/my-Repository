# mininpu_rt:Python 侧 runtime 封装(README §13 步骤 8 验收)。
#
#   import mininpu_rt
#   outs = mininpu_rt.run("mlp.minisa", {"x": x_f16}, spec="configs/npu256.json")
#
# 第一期实现走 minirt 子进程(模拟器是 C++ 二进制);输入按 meta.json 的
# name 对齐,临时落盘 npy 并以绝对路径写进 meta 副本,输出读回 numpy。
# 第二期可换 ctypes 直调 Runtime/Stream C API,接口不变。
import json
import os
import subprocess
import tempfile

import numpy as np

_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_DEFAULT_MINIRT = os.path.join(_REPO, "..", "build", "minirt")


class RuntimeError_(RuntimeError):
    pass


def _meta_path_of(bin_path):
    assert bin_path.endswith(".minisa"), "bin 必须是 .minisa"
    return bin_path[:-7] + ".meta.json"


def run(bin_path, inputs, spec=None, minirt=None, timeline=None):
    """跑一个已编译的 minisa。

    inputs: dict[str, np.ndarray] —— 键为 meta.json 里的输入 name(缺省 arg<id>)。
    spec:   HardwareSpec JSON 路径(None = 编译期内置默认)。
    timeline: 可选,chrome trace 输出路径。
    返回:   dict[str, np.ndarray] —— 键为输出 name(缺省 out<id>)。
    """
    minirt = minirt or os.path.abspath(_DEFAULT_MINIRT)
    meta_path = _meta_path_of(bin_path)
    with open(meta_path) as f:
        meta = json.load(f)

    with tempfile.TemporaryDirectory() as tmp:
        patched = dict(meta)
        patched_inputs = []
        for entry in meta.get("inputs", []):
            name = entry.get("name", f"arg{entry['id']}")
            if name not in inputs:
                raise RuntimeError_(f"缺输入 '{name}'(meta 声明: "
                                    f"{[e.get('name', e['id']) for e in meta.get('inputs', [])]})")
            arr = np.ascontiguousarray(inputs[name])
            npy = os.path.join(tmp, f"{name}.npy")
            np.save(npy, arr)
            patched_inputs.append({"id": entry["id"], "name": name, "npy": npy})
        patched["inputs"] = patched_inputs
        patched_meta = os.path.join(tmp, "patched.meta.json")
        with open(patched_meta, "w") as f:
            json.dump(patched, f)

        out_path = os.path.join(tmp, "out.npy")
        cmd = [minirt, "--bin", bin_path, "--meta", patched_meta,
               "--dump-out", out_path]
        if spec:
            cmd += ["--spec", spec]
        tl_path = timeline or os.path.join(tmp, "tl.json")
        cmd += ["--dump-timeline", tl_path]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError_(f"minirt 失败:\n{r.stderr}")

        outputs = {}
        outs = meta.get("outputs", [])
        for entry in outs:
            name = entry.get("name", f"out{entry['id']}")
            path = out_path if len(outs) == 1 else out_path[:-4] + f"_{entry['id']}.npy"
            outputs[name] = np.load(path)
        return outputs
