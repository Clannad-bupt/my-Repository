import numpy as np
import sympy as sp
from typing import List, Dict, Any
from .base import BaseOperator

class HornerOperator(BaseOperator):
    """
    [ADPC V5.4] 稳定版 Horner 算子。
    移除了 prepare_input 中的数据类型硬编码。
    """
    # 布局定义
    BANK_IN    = 0
    BANK_COEFF = 1
    BANK_OUT   = 2

    def __init__(self, coeffs: List[float]):
        self.coeffs = coeffs # [c_n, c_n-1, ..., c0]
        self.order = len(coeffs) - 1

    @property
    def tensor_configs(self) -> Dict[str, Dict[str, Any]]:
        return {
            "x":      {"bank": self.BANK_IN,    "role": "input"},
            "coeffs": {"bank": self.BANK_COEFF, "role": "param"},
            "y":      {"bank": self.BANK_OUT,   "role": "output"}
        }

    def get_dfg(self, spec: Any = None) -> List[Any]:
        x = sp.Symbol('x')
        c = [sp.Symbol(f'c{i}') for i in range(len(self.coeffs))]
        formula = c[0]
        for i in range(1, len(self.coeffs)):
            formula = formula * x + c[i]

        inputs_meta = self.build_inputs_meta({"x": x, "coeffs": c}, spec)
        return self.compile_with_adpc(formula, inputs_meta, out_addr=self.get_bank_addr("y", spec), spec=spec)

    def get_golden(self, input_data: Dict[str, np.ndarray], vector_lanes: int) -> Dict[str, Any]:
        data_x = input_data["x"]
        golden_y = np.polyval(self.coeffs, data_x)
        golden_intermediates = []
        num_iterations = len(data_x) // vector_lanes
        for iter_idx in range(num_iterations):
            x_val = data_x[iter_idx * vector_lanes]
            golden_intermediates.append({"iter": iter_idx, "x": float(x_val)})
        return {"y": golden_y, "intermediates": golden_intermediates}

    def prepare_input(self, n_rows: int) -> Dict[str, np.ndarray]:
        # 🛸 [V5.4 FIX] 使用 self.dtype 动态映射，移除 np.float32
        data_x = np.linspace(0.1, 1.6, n_rows).astype(self.dtype)
        return {
            "x": data_x,
            "coeffs": self.coeffs
        }
