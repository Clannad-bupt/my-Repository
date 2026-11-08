from typing import Dict, Any, List, Optional
import re

class BehavioralEngine:
    """
    语义执行引擎 (Behavioral Engine)
    负责解析并模拟 YAML Spec 中定义的 behavioral_semantics。
    实现“Spec-as-Logic”的硬件行为闭环。
    """
    def __init__(self, unit_spec: Any):
        self.spec = unit_spec
        self.behaviors = unit_spec.behavioral_semantics if hasattr(unit_spec, 'behavioral_semantics') else None

    def find_active_mode(self, decoded_instr: Any) -> Optional[Dict[str, Any]]:
        """根据当前解码后的指令，寻找匹配的执行模式"""
        if not self.behaviors or 'execution_modes' not in self.behaviors:
            return None
            
        for mode in self.behaviors['execution_modes']:
            condition = mode.get('condition', 'True')
            # 简单的表达式求值 (安全起见，这里可以后续增强为 AST 解析)
            # 目前支持: "FIELD == 'VALUE'"
            if self._eval_condition(condition, decoded_instr):
                return mode
        return None

    def _eval_condition(self, condition: str, instr: Any) -> bool:
        """评估条件字符串"""
        # 支持: VALID == 'VALID'
        pattern = r"(\w+)\s*==\s*(?:'([^']+)'|(\w+))"
        match = re.match(pattern, condition)
        if match:
            field, val_str, val_raw = match.groups()
            target_value = val_str if val_str is not None else val_raw
            actual_value = str(getattr(instr, field, None))
            return actual_value == target_value
            
        # 复杂模式支持 (AND)
        if "and" in condition:
            parts = condition.split("and")
            return all(self._eval_condition(p.strip(), instr) for p in parts)
            
        return False

    def get_layout(self, layout_name: str) -> Dict[str, Any]:
        """平衡数据布局定义"""
        layouts = self.behaviors.get('data_layouts', {})
        return layouts.get(layout_name, {})

    def slice_data(self, data: int, layout: Dict[str, Any], slice_range: Optional[List[int]] = None) -> List[int]:
        """根据布局和分片范围，将 128-bit 数据切分为原始元素列表"""
        width = layout.get('width', 32)
        count = layout.get('count', 4)
        
        elements = []
        for i in range(count):
            elements.append((data >> (i * width)) & ((1 << width) - 1))
            
        if slice_range:
            return elements[slice_range[0]:slice_range[1]]
        return elements
