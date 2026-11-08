#!/usr/bin/env python3
"""步骤 9 验收:Partition 与 Fallback(README §13)。

  test_softmax_fallback: §14.1 MLP 的 rms_norm 后插入 softmax(非白名单)。
    期望:整图结果与 PyTorch FP32 一致;NPU 恰好执行前后两个 supported 段
    (rms 分量 + matmul 链分量),softmax 落在 CPU eager;共 2 次 NPU launch。
  test_torch_compile_backend: §7.4 加分项,torch.compile(backend=mininpu_backend)。
"""
import os
import sys
import tempfile

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPEC = os.path.join(REPO, "configs", "npu256.json")
sys.path.insert(0, os.path.join(REPO, "python"))
sys.path.insert(0, os.path.join(REPO, "examples"))

try:
    import torch
    import torch.nn.functional as F
    from llama_mlp import LlamaMLP, rmsnorm
    from torch_bridge import compile_graph, mininpu_backend
except ImportError as e:
    print(f"SKIP: {e}")
    sys.exit(0)


# softmax 后的激活量级极小(silu·up 乘积 ~1e-5,落在 f16 次正规区),
# 容差必须非空洞:全零输出在 atol=1e-2 下也会「通过」,故收紧 atol 并
# 显式断言输出量级,防止 FTZ/清零类回归被容差掩盖。
ATOL, RTOL = 1e-5, 5e-2


def _assert_not_vacuous(out, ref):
    assert np.abs(out.numpy()).sum() > 0.5 * np.abs(ref.numpy()).sum(), \
        "输出量级异常(疑似全零/清零回归),拒绝空洞通过"


class MlpWithSoftmax(LlamaMLP):
    def forward(self, x):
        x = rmsnorm(x, self.norm_gamma, self.eps)
        x = torch.softmax(x, dim=-1)  # 非白名单 → CPU fallback
        gate = F.silu(x @ self.w_gate)
        up = x @ self.w_up
        return (gate * up) @ self.w_down


def test_softmax_fallback(tmp):
    torch.manual_seed(0)
    model = MlpWithSoftmax(256, 512).eval()
    x32 = torch.randn(32, 256)
    with torch.no_grad():
        ref = model(x32)

    ep = torch.export.export(model, (x32,))
    work = os.path.join(tmp, "part")
    cg = compile_graph(ep, work, name="mlp_sm", spec=SPEC)
    assert len(cg.components) == 2, \
        f"softmax 应把图切成 2 个 supported 连通块,实际 {len(cg.components)}"

    out = cg(x32)
    assert cg.last_npu_launches == 2, f"应恰好 2 次 NPU launch,实际 {cg.last_npu_launches}"
    _assert_not_vacuous(out, ref)
    np.testing.assert_allclose(out.numpy(), ref.numpy(), atol=ATOL, rtol=RTOL)

    # 每个分量都有完整产物
    for comp in cg.components:
        for ext in (".minisa", ".meta.json", ".report.md"):
            assert os.path.exists(comp.bin[:-7] + ext), f"{comp.name} 缺 {ext}"
    print("PASS test_softmax_fallback (2 NPU partitions + CPU softmax)")


def test_torch_compile_backend(tmp):
    torch.manual_seed(0)
    model = MlpWithSoftmax(256, 512).eval()
    x32 = torch.randn(32, 256)
    with torch.no_grad():
        ref = model(x32)

    opt = torch.compile(model, backend=mininpu_backend(os.path.join(tmp, "tc"),
                                                       spec=SPEC))
    out = opt(x32)
    _assert_not_vacuous(out, ref)
    np.testing.assert_allclose(out.numpy(), ref.numpy(), atol=ATOL, rtol=RTOL)
    print("PASS test_torch_compile_backend (§7.4)")


def test_eager_fallback_call_method(tmp):
    """非 call_function 节点(call_method)应 eager 兜底而非报错。

    回归背景:旧实现在 __call__ 遇到 op != call_function 直接 raise,
    与「覆盖不了就落 CPU」的 graceful degradation 矛盾。
    torch.export 规范化后此类节点少见,用合成 FX 图锁定语义。
    """
    from torch_bridge.partition import CompiledGraph

    # 真实 fx.Graph(_value 等处的 isinstance(fx.Node) 判定需要真节点)
    g = torch.fx.Graph()
    px = g.placeholder("x")
    up = g.call_method("unsqueeze", (px, 0))
    g.output((up,))  # args[0] 须为 tuple(torch.export 的形态)
    gm = torch.fx.GraphModule({}, g)

    class FakeSig:
        parameters, buffers = [], []

    class FakeEP:
        graph_module = gm
        graph_signature = FakeSig()
        state_dict = {}

    cg = CompiledGraph(FakeEP(), os.path.join(tmp, "fb"), spec=SPEC)
    assert cg.components == [], "全 call_method 的图不应该有 NPU 分量"
    inp = torch.randn(2, 4)
    got = cg(inp)
    assert isinstance(got, torch.Tensor) and got.shape == (1, 2, 4)
    torch.testing.assert_close(got, inp.unsqueeze(0))
    assert cg.last_npu_launches == 0
    print("PASS test_eager_fallback_call_method (call_method → eager)")


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as tmp:
        test_softmax_fallback(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_torch_compile_backend(tmp)
    with tempfile.TemporaryDirectory() as tmp:
        test_eager_fallback_call_method(tmp)
    print("ALL PARTITION TESTS PASS")
