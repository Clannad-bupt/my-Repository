import numpy as np
import sympy as sp
from typing import List, Dict, Any
from .base import BaseOperator

class TaylorOperator(BaseOperator):
    """
    [ADPC V5.1] 工业级 Taylor 算子。
    完全移除硬编码，支持动态精度与动态物理布局。
    """
    def __init__(self, coeffs: Dict[int, float]):
        self.sparse_coeffs = dict(sorted(coeffs.items(), key=lambda x: x[0], reverse=True))
        self.powers = list(self.sparse_coeffs.keys())
        self.coeff_values = list(self.sparse_coeffs.values())

    @property
    def tensor_configs(self) -> Dict[str, Dict[str, Any]]:
        return {
            "x":      {"bank": 0, "role": "input"},
            "coeffs": {"bank": 1, "role": "param"},
            "y":      {"bank": 2, "role": "output"}
        }

    def get_dfg(self, spec: Any = None) -> List[Any]:
        x = sp.Symbol('x')
        c = [sp.Symbol(f'c{p}') for p in self.powers]
        
        formula = c[0]
        for i in range(len(self.powers) - 1):
            diff = self.powers[i] - self.powers[i+1]
            formula = formula * (x**diff) + c[i+1]
        
        last_power = self.powers[-1]
        if last_power > 0:
            formula = formula * (x**last_power)
            
        inputs_meta = self.build_inputs_meta({"x": x, "coeffs": c}, spec)
        return self.compile_with_adpc(formula, inputs_meta, out_addr=self.get_bank_addr("y", spec), spec=spec)

    def get_golden(self, input_data: Dict[str, np.ndarray], vector_lanes: int) -> Dict[str, Any]:
        data_x = input_data["x"]
        golden_y = np.zeros_like(data_x)
        for p, val in self.sparse_coeffs.items():
            golden_y += val * (data_x ** p)
        return {"y": golden_y, "intermediates": []}

    def prepare_input(self, n_rows: int) -> Dict[str, np.ndarray]:
        # 🛸 [V5.1 FIX] 使用动态 dtype，移除 np.float32 硬编码
        data_x = np.linspace(0.1, 1.2, n_rows).astype(self.dtype)
        return {
            "x": data_x,
            "coeffs": self.coeff_values
        }
