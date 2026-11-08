import z3
from typing import Any

class AxiomBase:
    _label_idx = 0

    @staticmethod
    def _add_constraint(solver, constraint: Any, label_name: str):
        AxiomBase._label_idx += 1
        unique_label = f"{label_name}_{AxiomBase._label_idx}"
        if isinstance(solver, (z3.Solver, z3.Optimize)):
            ctx = solver.ctx
            solver.assert_and_track(constraint, z3.Bool(unique_label, ctx=ctx))
        else:
            solver.add(constraint)
