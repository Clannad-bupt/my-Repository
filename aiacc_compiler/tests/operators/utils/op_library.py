# tests/operators/utils/op_library.py
import sympy as sp
import numpy as np
from typing import Dict, Any, Callable

"""
🛸 [ADPC V4.0] 算子公式库注册中心 (The Operator Library)
这里定义了算子的 SymPy 公式、Golden 计算函数以及建议的测试数据域。
"""

OP_DEFINITIONS: Dict[str, Dict[str, Any]] = {
    "reciprocal": {
        "symbols": ["x"],
        "formula": lambda x: 1.0 / x[0],
        "golden_fn": lambda data: 1.0 / data['x'],
        "input_range": (0.5, 1.5)
    },
    "softsign": {
        "symbols": ["x"],
        "formula": lambda x: x[0] / (1.0 + sp.Abs(x[0])),
        "golden_fn": lambda data: data['x'] / (1.0 + np.abs(data['x'])),
        "input_range": (-1.0, 1.0)
    },
    "square": {
        "symbols": ["x"],
        "formula": lambda x: x[0]**2,
        "golden_fn": lambda data: data['x']**2,
        "input_range": (-10.0, 10.0)
    },
    "add": {
        "symbols": ["x", "y"], # 二输入示例
        "formula": lambda x: x[0] + x[1],
        "golden_fn": lambda data: data['x'] + data['y'],
        "input_range": (-20.0, 20.0)
    }
}

def get_op_def(op_name):
    if op_name not in OP_DEFINITIONS:
        raise ValueError(f"Operator '{op_name}' not registered in ADPC library.")
    return OP_DEFINITIONS[op_name]
