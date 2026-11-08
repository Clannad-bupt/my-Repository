import sympy as sp
from typing import Callable, Dict, Any, List

class KernelRegistry:
    """
    AIACC 算子内核注册表 (V1.0)。
    用于将 SymPy 表达式模式动态映射到硬件指令生成逻辑。
    """
    _kernels: List[Dict[str, Any]] = []

    @classmethod
    def register(cls, pattern_func: Callable[[Any], bool]):
        """
        注册装饰器。
        pattern_func: 一个接收 SymPy 表达式并返回布尔值的函数，用于匹配。
        """
        def decorator(lower_func: Callable):
            cls._kernels.append({
                "match": pattern_func,
                "lower": lower_func
            })
            return lower_func
        return decorator

    @classmethod
    def lookup(cls, expr: Any) -> Callable:
        """
        根据表达式匹配合适的内核转换函数。
        """
        # 🛸 按照注册顺序逆序匹配（允许后注册的覆盖先注册的）
        for kernel in reversed(cls._kernels):
            if kernel["match"](expr):
                return kernel["lower"]
        return None
