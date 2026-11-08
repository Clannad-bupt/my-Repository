# torch_bridge:FX/torch.export → minigraph MLIR 文本 + weights manifest(README §7、§13 步骤 2)。
#
# 用法:
#   ep = torch.export.export(model, (x,))
#   export_to_minigraph(ep, "/tmp/mlp", name="mlp")        # 整图直译(步骤 2)
#   cg = compile_graph(ep, "/tmp/mlp_part")                # 染色+切分+逐分量编译(步骤 9)
#   y = cg(x)                                              # supported→NPU,其余 torch eager
from .fx_to_minigraph import UnsupportedError, emit_subgraph, export_to_minigraph
from .partition import CompiledGraph, compile_graph, mininpu_backend

__all__ = ["export_to_minigraph", "emit_subgraph", "UnsupportedError",
           "compile_graph", "CompiledGraph", "mininpu_backend"]
