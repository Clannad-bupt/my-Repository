import yaml
import os
import numpy as np
import sympy as sp
from typing import List, Dict, Any
from .base import BaseOperator

class ConfigurableOperator(BaseOperator):
    """
    [ADPC V5.0] 高级抽象算子。
    允许通过 YAML 配置文件定义公式、布局和测试逻辑，实现 70+ 算子的配置化扩展。
    """
    def __init__(self, config_path: str):
        with open(config_path, 'r', encoding='utf-8') as f:
            self.cfg = yaml.safe_load(f)
        
        self.name = self.cfg.get("name", "anon_op")
        self.formula_str = self.cfg["formula"]
        self.tensors_cfg = self.cfg["tensors"]
        self.test_cfg = self.cfg.get("test_data", {})
        
        # 🛸 [V5.0 FIX] 构建 locals，强制把 tensors_cfg 里的名字当作 Symbol，防止 gamma/beta 等与 SymPy 内置冲突
        locals_dict = {name: sp.Symbol(name) for name in self.tensors_cfg}
        self.formula = sp.sympify(self.formula_str, locals=locals_dict)

    @property
    def tensor_configs(self) -> Dict[str, Dict[str, Any]]:
        return self.tensors_cfg

    @property
    def element_bits(self) -> int:
        # 🛸 [V6.5 FIX] 动态解析 YAML 精度配置，解决 FP16/INT8 等多精度混合场景下的硬编码问题
        for name, config in self.tensors_cfg.items():
            prec = config.get("precision") or config.get("element_bits")
            if prec:
                if isinstance(prec, int): return prec
                prec = str(prec).lower()
                if "16" in prec: return 16
                if "8" in prec: return 8
                if "4" in prec: return 4
        return 32

    def get_dfg(self, spec: Any = None) -> List[Any]:
        # 1. 解析公式
        expr = self.formula
        symbols = {str(s): s for s in expr.free_symbols}
        
        # 2. 映射符号到物理元数据
        inputs_map = {}
        for sym_name, sym_obj in symbols.items():
            if sym_name in self.tensors_cfg:
                inputs_map[sym_name] = sym_obj
            else:
                # 处理未显式定义的符号
                inputs_map[sym_name] = sym_obj
        
        inputs_meta = self.build_inputs_meta(inputs_map, spec)
        
        # 🛸 [V5.0 FIX] 自动或显式为常量分配 Bank (处理字面量)
        if "constants" not in inputs_meta:
            const_cfg = self.tensors_cfg.get("constants")
            if const_cfg:
                const_bank = const_cfg["bank"]
                offset = const_cfg.get("offset", 0)
            else:
                const_bank = self._find_free_bank(spec)
                offset = 0
                
            bank_depth = self._get_hw_param(spec, "PARAM_UR_BANK_DEPTH", 256)
            if isinstance(const_bank, list):
                logical_addr = const_bank[0] * bank_depth + offset
                logical_addr_list = [b * bank_depth + offset for b in const_bank]
            else:
                logical_addr = const_bank * bank_depth + offset
                logical_addr_list = None
                
            inputs_meta["constants"] = {
                "logical_addr": logical_addr,
                "stride": 0,
                "role": "constant"
            }
            if logical_addr_list is not None:
                inputs_meta["constants"]["logical_addr_list"] = logical_addr_list
        
        # 3. 编译
        out_name = "y" 
        return self.compile_with_adpc(
            expr, 
            inputs_meta, 
            out_name=out_name,
            out_addr=self.get_bank_addr(out_name, spec), 
            spec=spec
        )

    def get_golden(self, input_data: Dict[str, np.ndarray], vector_lanes: int) -> Dict[str, Any]:
        if self.name == "matmul":
            # [V8.0] 对齐真实硬件行为：
            # 每次外层迭代（vector_lanes 行）执行 4 次独立计算（step k, k=0..3）：
            #   FPMUL: [ak*bk] 4-lane 向量乘法
            #   ADDTREE 通道0: S0[0]+S0[1]+S0[2]+S0[3]+old_0
            # pipeline latency (4 cycles) 自然将相邻 step 的 old_0 隔离为 0，
            # 因此 4 个输出位置各自独立：
            #   y[start+k] = sum(ak[start:end] * bk[start:end])  (k=0..3)
            a0 = input_data["a0"]
            a1 = input_data["a1"]
            a2 = input_data["a2"]
            a3 = input_data["a3"]
            b0 = input_data["b0"]
            b1 = input_data["b1"]
            b2 = input_data["b2"]
            b3 = input_data["b3"]

            n_rows = len(a0)
            golden_y = np.zeros(n_rows, dtype=np.float32)
            for iter_idx in range(n_rows // vector_lanes):
                start = iter_idx * vector_lanes
                end   = start + vector_lanes
                golden_y[start + 0] = np.sum(a0[start:end] * b0[start:end])
                golden_y[start + 1] = np.sum(a1[start:end] * b1[start:end])
                golden_y[start + 2] = np.sum(a2[start:end] * b2[start:end])
                golden_y[start + 3] = np.sum(a3[start:end] * b3[start:end])
            return {"y": golden_y, "intermediates": []}

        expr = self.formula
        # 获取排序后的符号列表
        sorted_symbols = sorted([str(s) for s in expr.free_symbols])
        func = sp.lambdify([sp.Symbol(s) for s in sorted_symbols], expr, "numpy")
        
        # 提取参数，支持标量广播
        args = {}
        for s in sorted_symbols:
            val = input_data.get(s)
            if val is not None:
                args[s] = val
        
        golden_y = func(**args)
        
        # 🛸 [ALGORITHM OPTIMIZATION] 支持多输出/矩阵算子：转换形如 (num_outputs, 1, n_rows) 的矩阵结果为 (n_rows, num_outputs) 布局
        if isinstance(golden_y, list) or (hasattr(golden_y, "ndim") and golden_y.ndim > 1):
            arr = np.array(golden_y)
            if arr.ndim == 3 and arr.shape[1] == 1:
                golden_y = np.squeeze(arr, axis=1).T
        
        return {
            "y": golden_y, 
            "intermediates": [] 
        }

    def prepare_input(self, n_rows: int) -> Dict[str, np.ndarray]:
        inputs = {}
        addr_to_data = {}
        for name, cfg in self.tensors_cfg.items():
            if cfg["role"] == "output": continue
            
            bank_key = tuple(cfg["bank"]) if isinstance(cfg["bank"], list) else cfg["bank"]
            addr_key = (bank_key, cfg.get("offset", 0))
            if addr_key in addr_to_data:
                inputs[name] = addr_to_data[addr_key]
                continue
            
            t_cfg = self.test_cfg.get(name, {})
            if isinstance(t_cfg, list):
                # 固定参数
                data = np.array(t_cfg).astype(self.dtype)
            elif isinstance(t_cfg, dict) and "range" in t_cfg:
                # 范围生成
                r = t_cfg["range"]
                data = np.linspace(r[0], r[1], n_rows).astype(self.dtype)
            else:
                # 默认随机
                data = np.random.randn(n_rows).astype(self.dtype)
                
            inputs[name] = data
            addr_to_data[addr_key] = data
        return inputs

    def inject_data(self, program: Any, input_data: Dict[str, np.ndarray], vector_lanes: int, spec: Any = None):
        # 1. 注入常规张量
        super().inject_data(program, input_data, vector_lanes, spec)
        
        # 2. 🛸 [V6.3 FIX] 统一且鲁棒的编译期常量注入
        # 弃用老旧的手动 SymPy 公式字面量提取，直接查询编译器实际使用的 const_pool，
        # 并通过 self.ops 检索其实际分配的 logical_addr 地址进行内存注入，
        # 完美解决内核底层动态生成常量（如 Abs 展开引入的 -1.0）导致的漏注/内存污染问题
        if hasattr(self, "ops") and hasattr(self, "compiler") and hasattr(self.compiler, "const_pool"):
            op_map = {op.trace_id: op for op in self.ops}
            for val_f, node_id in self.compiler.const_pool.items():
                if node_id in op_map:
                    node = op_map[node_id]
                    if "logical_addr" in node.metadata:
                        addr = node.metadata["logical_addr"]
                        program.inject_broadcast(addr, val_f)
