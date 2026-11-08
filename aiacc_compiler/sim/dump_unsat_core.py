import os
import sys
import z3

sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'src'))

from api.hw_spec import HardwareSpec
from ir.mir.mir_op import MIROp
from synthesis.backend import SMTSynthesizer
from synthesis.axioms import SynthesisAxioms

# 复用测试用例的算子
ops = [
    MIROp(op_type="UR_READ", logic_cycle=0, trace_id="read_a", metadata={}),
    MIROp(op_type="UR_READ", logic_cycle=0, trace_id="read_b", metadata={}),
    MIROp(op_type="UR_READ", logic_cycle=0, trace_id="read_c", metadata={}),
    MIROp(op_type="RTOVR", logic_cycle=1, trace_id="rt_a", metadata={"depends_on": ["read_a"]}),
    MIROp(op_type="RTOVR", logic_cycle=1, trace_id="rt_b", metadata={"depends_on": ["read_b"]}),
    MIROp(op_type="RTOVR", logic_cycle=1, trace_id="rt_c", metadata={"depends_on": ["read_c"]}),
    MIROp(op_type="FPMUL", logic_cycle=2, trace_id="mul_ab", metadata={"depends_on": ["rt_a", "rt_b"]}),
    MIROp(op_type="RTOVR", logic_cycle=3, trace_id="rt_mul", metadata={"depends_on": ["mul_ab"]}),
    MIROp(op_type="FPADD", logic_cycle=4, trace_id="add_c", metadata={"depends_on": ["rt_mul", "rt_c"]}),
    MIROp(op_type="RTOVR", logic_cycle=5, trace_id="rt_add", metadata={"depends_on": ["add_c"]}),
    MIROp(op_type="UR_WRITE", logic_cycle=6, trace_id="write_out", metadata={"depends_on": ["rt_add"]})
]

def main():
    hw_config_dir = os.path.join(os.path.dirname(__file__), '..', 'hw_config')
    spec = HardwareSpec.load(hw_config_dir)
    synth = SMTSynthesizer(spec)
    
    II = 1
    solver = z3.Solver()
    solver.set(unsat_core=True)
    
    nodes_by_id = {op.trace_id: op for op in ops}
    t = {op.trace_id: z3.Int(f"t_{op.trace_id}") for op in ops}
    registry = synth.hw_spec.config.get("iq_registry", {})
    
    s_vars = {}
    for op_id, t_var in t.items():
        s = z3.Int(f"s_{op_id}")
        k = z3.Int(f"k_{op_id}")
        solver.assert_and_track(t_var == k * II + s, f'mod_{op_id}')
        solver.assert_and_track(s >= 0, f'sge0_{op_id}')
        solver.assert_and_track(s < II, f'slt_{op_id}')
        solver.assert_and_track(k >= 0, f'kge0_{op_id}')
        s_vars[op_id] = s

    iq_vars = {}
    for op in ops:
        res_key = None
        if "iq_type" in op.metadata:
            res_meta = op.metadata["iq_type"]
            res_key = res_meta.name if hasattr(res_meta, 'name') else str(res_meta)
        else:
            res_key = str(op.op_type).upper()
        
        valid_iqs = synth.hw_spec.get_iq_ids(res_key)
        if valid_iqs:
            iv = z3.Int(f"iq_{op.trace_id}")
            iq_vars[op.trace_id] = iv
            solver.assert_and_track(z3.Or([iv == iq_id for iq_id in valid_iqs]), f'valid_iq_{op.trace_id}')
            
            if op.metadata.get("iq_id") is not None:
                solver.assert_and_track(iv == op.metadata["iq_id"], f'manual_iq_{op.trace_id}')

    for op in ops:
        for dep_id in op.metadata.get("depends_on", []):
            if dep_id in t:
                dep_node = nodes_by_id[dep_id]
                delay = synth.norm_delays.get(dep_node.op_type.upper(), 1)
                transit = 2 if dep_node.op_type.upper() == "UR_WRITE" else 1
                solver.assert_and_track(t[op.trace_id] >= t[dep_id] + (delay - 1) + transit, f'raw_{dep_id}_{op.trace_id}')

    pass_s = s_vars
    
    # 手动展开公理，分别加上追踪标识
    # 读端口互斥
    read_ops = [o for o in ops if o.op_type.upper() == "UR_READ"]
    for i in range(len(read_ops)):
        for j in range(i + 1, len(read_ops)):
            op_A, op_B = read_ops[i], read_ops[j]
            if op_A.trace_id not in iq_vars or op_B.trace_id not in iq_vars: continue
            same_port = iq_vars[op_A.trace_id] == iq_vars[op_B.trace_id]
            var_A = pass_s[op_A.trace_id]
            var_B = pass_s[op_B.trace_id]
            solver.assert_and_track(z3.Implies(same_port, var_A != var_B), f'read_mutex_{op_A.trace_id}_{op_B.trace_id}')
            
    # Latch protect
    for i in range(len(read_ops)):
        for j in range(i + 1, len(read_ops)):
            op_A, op_B = read_ops[i], read_ops[j]
            if op_A.trace_id not in iq_vars or op_B.trace_id not in iq_vars: continue
            same_iq = iq_vars[op_A.trace_id] == iq_vars[op_B.trace_id]
            
            def get_pw(op_x):
                cons = [o for o in ops if op_x.trace_id in o.metadata.get("depends_on", [])]
                if not cons: return t[op_x.trace_id]
                m = t[cons[0].trace_id]
                for c in cons[1:]: m = z3.If(t[c.trace_id] > m, t[c.trace_id], m)
                return m
            
            t_end_A = get_pw(op_A)
            t_end_B = get_pw(op_B)
            c_B_after_A = t[op_B.trace_id] > t_end_A
            c_A_after_B = t[op_A.trace_id] > t_end_B
            solver.assert_and_track(z3.Implies(same_iq, z3.Or(c_B_after_A, c_A_after_B)), f'latch_prot_{op_A.trace_id}_{op_B.trace_id}')

    # Anti-overwrite
    for op in ops:
        cons = [o for o in ops if op.trace_id in o.metadata.get("depends_on", [])]
        if not cons: continue
        m_val = t[cons[0].trace_id]
        for c in cons[1:]: m_val = z3.If(t[c.trace_id] > m_val, t[c.trace_id], m_val)
        lat = spec.delays.get(op.op_type.upper(), 1)
        data_ready = t[op.trace_id] + lat - 1
        solver.assert_and_track(m_val - data_ready <= II, f'anti_ow_{op.trace_id}')

    # Shadow bind
    for op in ops:
        rt_deps = [d for d in op.metadata.get("depends_on", []) if d in nodes_by_id and nodes_by_id[d].op_type == "RTOVR"]
        if rt_deps and op.op_type.upper() in ["FPMUL", "FPADD", "UR_WRITE"]:
            for rd in rt_deps:
                solver.assert_and_track(t[op.trace_id] >= t[rd] + 1, f'shadow_lo_{rd}_{op.trace_id}')
                solver.assert_and_track(t[op.trace_id] <= t[rd] + 4, f'shadow_hi_{rd}_{op.trace_id}')

    res = solver.check()
    print("Result:", res)
    if res == z3.unsat:
        core = solver.unsat_core()
        print("UNSAT CORE:")
        for c in core: print(c)

if __name__ == "__main__":
    main()
