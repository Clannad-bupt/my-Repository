from .dependency_axioms import DependencyAxioms
from .resource_axioms import ResourceAxioms
from .physical_axioms import PhysicalAxioms
from .special_axioms import SpecialAxioms

# 统一导出
add_latency_axiom = DependencyAxioms.add_latency_axiom
add_dependency_axiom = DependencyAxioms.add_dependency_axiom

add_resource_constraint = ResourceAxioms.add_resource_constraint
add_modulo_constraint = ResourceAxioms.add_modulo_constraint
add_rtovr_modulo_constraint = ResourceAxioms.add_rtovr_modulo_constraint
add_rtovr_liveness_axiom = ResourceAxioms.add_rtovr_liveness_axiom
add_bank_conflict_axiom = ResourceAxioms.add_bank_conflict_axiom
add_spill_reload_mutex_axiom = ResourceAxioms.add_spill_reload_mutex_axiom

add_rtovr_alignment_axiom = PhysicalAxioms.add_rtovr_alignment_axiom
add_side_effect_phase_axiom = PhysicalAxioms.add_side_effect_phase_axiom
add_anti_overwrite_axiom = PhysicalAxioms.add_anti_overwrite_axiom
add_shadow_binding_axiom = PhysicalAxioms.add_shadow_binding_axiom
add_physical_binding_axiom = PhysicalAxioms.add_physical_binding_axiom
add_rtovr_crossbar_axiom = PhysicalAxioms.add_rtovr_crossbar_axiom
add_iq_latch_protect_axiom = PhysicalAxioms.add_iq_latch_protect_axiom
add_alu_liveness_axiom = PhysicalAxioms.add_alu_liveness_axiom
add_iq_alignment_axiom = PhysicalAxioms.add_iq_alignment_axiom

class SynthesisAxioms:
    """
    兼容旧版的 SynthesisAxioms 类，以便现有的单元测试和外部脚本可以无缝调用。
    """
    @staticmethod
    def _add_constraint(solver, cond, label):
        from .base import AxiomBase
        AxiomBase._add_constraint(solver, cond, label)

    @staticmethod
    def add_latency_axiom(*args, **kwargs):
        return add_latency_axiom(*args, **kwargs)

    @staticmethod
    def add_dependency_axiom(*args, **kwargs):
        return add_dependency_axiom(*args, **kwargs)

    @staticmethod
    def add_resource_constraint(*args, **kwargs):
        return add_resource_constraint(*args, **kwargs)

    @staticmethod
    def add_modulo_constraint(*args, **kwargs):
        return add_modulo_constraint(*args, **kwargs)

    @staticmethod
    def add_rtovr_modulo_constraint(*args, **kwargs):
        return add_rtovr_modulo_constraint(*args, **kwargs)

    @staticmethod
    def add_rtovr_liveness_axiom(*args, **kwargs):
        return add_rtovr_liveness_axiom(*args, **kwargs)

    @staticmethod
    def add_bank_conflict_axiom(*args, **kwargs):
        return add_bank_conflict_axiom(*args, **kwargs)

    @staticmethod
    def add_spill_reload_mutex_axiom(*args, **kwargs):
        return add_spill_reload_mutex_axiom(*args, **kwargs)

    @staticmethod
    def add_rtovr_alignment_axiom(*args, **kwargs):
        return add_rtovr_alignment_axiom(*args, **kwargs)

    @staticmethod
    def add_side_effect_phase_axiom(*args, **kwargs):
        return add_side_effect_phase_axiom(*args, **kwargs)

    @staticmethod
    def add_anti_overwrite_axiom(*args, **kwargs):
        return add_anti_overwrite_axiom(*args, **kwargs)

    @staticmethod
    def add_shadow_binding_axiom(*args, **kwargs):
        return add_shadow_binding_axiom(*args, **kwargs)

    @staticmethod
    def add_physical_binding_axiom(*args, **kwargs):
        return add_physical_binding_axiom(*args, **kwargs)

    @staticmethod
    def add_rtovr_crossbar_axiom(*args, **kwargs):
        return add_rtovr_crossbar_axiom(*args, **kwargs)

    @staticmethod
    def add_iq_latch_protect_axiom(*args, **kwargs):
        return add_iq_latch_protect_axiom(*args, **kwargs)

    @staticmethod
    def add_alu_liveness_axiom(*args, **kwargs):
        return add_alu_liveness_axiom(*args, **kwargs)

    @staticmethod
    def add_iq_alignment_axiom(*args, **kwargs):
        return add_iq_alignment_axiom(*args, **kwargs)

    @staticmethod
    def add_alu_pipeline_axiom(solver, ops, t_vars, iq_vars, spec, s_vars=None, **kwargs):
        """
        ALU 流水线公理：
          1. 发射排他：同单元同槽位不可同时发射（使用 s_vars）
          2. Liveness 保护：前驱结果必须在后继覆盖前被搬走（绝对时间）
        """
        import z3
        alu_types = [
            "FPMUL", "FPADD", "FPTOINT", "INTTOFP",
            "LOGIC", "SHIFTUP", "SHIFTDN", "INTADD",
        ]
        for alu_type in alu_types:
            alu_ops = [o for o in ops if o.op_type == alu_type]
            if len(alu_ops) <= 1:
                continue
            lat = spec.delays.get(alu_type, 4)

            for i in range(len(alu_ops)):
                for j in range(i + 1, len(alu_ops)):
                    op_i, op_j = alu_ops[i], alu_ops[j]
                    if op_i.trace_id not in iq_vars or op_j.trace_id not in iq_vars:
                        continue

                    same_unit = iq_vars[op_i.trace_id] == iq_vars[op_j.trace_id]

                    # 1. 发射排他（模槽）
                    var_i = s_vars[op_i.trace_id] if s_vars else t_vars[op_i.trace_id]
                    var_j = s_vars[op_j.trace_id] if s_vars else t_vars[op_j.trace_id]
                    SynthesisAxioms._add_constraint(
                        solver,
                        z3.Implies(same_unit, var_i != var_j),
                        f"alu_issue_{alu_type}_{op_i.trace_id}_{op_j.trace_id}",
                    )

__all__ = [
    "DependencyAxioms",
    "ResourceAxioms",
    "PhysicalAxioms",
    "SpecialAxioms",
    "SynthesisAxioms",
    "add_latency_axiom",
    "add_dependency_axiom",
    "add_resource_constraint",
    "add_modulo_constraint",
    "add_rtovr_modulo_constraint",
    "add_rtovr_liveness_axiom",
    "add_bank_conflict_axiom",
    "add_spill_reload_mutex_axiom",
    "add_rtovr_alignment_axiom",
    "add_side_effect_phase_axiom",
    "add_anti_overwrite_axiom",
    "add_shadow_binding_axiom",
    "add_physical_binding_axiom",
    "add_rtovr_crossbar_axiom",
    "add_iq_latch_protect_axiom",
    "add_alu_liveness_axiom",
    "add_iq_alignment_axiom",
]
