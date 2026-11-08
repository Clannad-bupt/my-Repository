import sys
from pathlib import Path
import sympy as sp

ws = Path.cwd()
sys.path.append(str(ws / "src"))

from library.atomic.transcendentals.sin import get_sin_standard_dfg
from frontend import GraphCompiler

def print_dfg(name, dfg):
    print(f"=== {name} DFG ({len(dfg)} nodes) ===")
    for node in dfg:
        deps = [d for d in node.metadata.get('depends_on', [])]
        meta = {k: v for k, v in node.metadata.items() if k not in ['depends_on', 'math']}
        print(f"{node.op_type: <10} | ID:{node.trace_id[:16]: <16} | DEPS:{deps} | META:{meta}")

def compare():
    old_dfg = get_sin_standard_dfg()
    
    compiler = GraphCompiler()
    x_in = sp.Symbol('x_in') 
    
    c_inv6 = 0.1666666667
    c_inv120 = 0.0083333333
    c_inv5040 = 0.0001984127
    c_inv362880 = 2.75573192e-06
    
    formula = x_in * (1.0 - x_in**2 * (c_inv6 - x_in**2 * (c_inv120 - x_in**2 * (c_inv5040 - x_in**2 * c_inv362880))))
    
    inputs_meta = { x_in: {"role": "operand_a"} }
    new_dfg = compiler.compile(inputs_meta, formula, out_name="out")
    
    print_dfg("OLD MANUAL", old_dfg)
    print("\n" + "="*100 + "\n")
    print_dfg("NEW AUTO", new_dfg)

if __name__ == "__main__":
    compare()
