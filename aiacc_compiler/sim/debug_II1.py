"""
诊断脚本：精确定位 II=1 UNSAT 的原因
直接内联 SMT 约束，绕过项目框架，逐步添加公理观察哪一步 UNSAT
"""
import sys
sys.path.append('src')
import z3

# 硬件参数（已从 hw_spec 确认）
DELAYS = {'UR_READ': 1, 'RTOVR': 1, 'FPMUL': 4, 'FPADD': 4, 'UR_WRITE': 1}
UR_READ_PORTS = [30, 31, 32, 33]  # 4 个读端口
II = 1

# 算子列表（与 test_mac_modulo.py 一致）
op_ids = ['read_a', 'read_b', 'read_c',
          'rt_a', 'rt_b', 'rt_c',
          'mul_ab', 'rt_mul',
          'add_c', 'rt_add', 'write_out']

op_types = {
    'read_a': 'UR_READ', 'read_b': 'UR_READ', 'read_c': 'UR_READ',
    'rt_a': 'RTOVR', 'rt_b': 'RTOVR', 'rt_c': 'RTOVR',
    'mul_ab': 'FPMUL', 'rt_mul': 'RTOVR',
    'add_c': 'FPADD', 'rt_add': 'RTOVR', 'write_out': 'UR_WRITE'
}

depends_on = {
    'read_a': [], 'read_b': [], 'read_c': [],
    'rt_a': ['read_a'], 'rt_b': ['read_b'], 'rt_c': ['read_c'],
    'mul_ab': ['rt_a', 'rt_b'],
    'rt_mul': ['mul_ab'],
    'add_c': ['rt_mul', 'rt_c'],
    'rt_add': ['add_c'],
    'write_out': ['rt_add']
}

def check(solver, label):
    result = solver.check()
    print(f"  [{label}]: {result}")
    if result == z3.unsat:
        core = solver.unsat_core()
        print(f"    UNSAT Core: {[str(c) for c in core]}")
    return result == z3.sat

solver = z3.Solver()

# 创建时间变量和模调度变量
t = {op: z3.Int(f't_{op}') for op in op_ids}
s = {op: z3.Int(f's_{op}') for op in op_ids}
k = {op: z3.Int(f'k_{op}') for op in op_ids}
iq = {op: z3.Int(f'iq_{op}') for op in ['read_a','read_b','read_c']}

print("=== 逐步添加公理诊断 ===\n")

# 1. 模调度线性化
print("[1] 添加模调度线性化约束 (t = k*II + s, 0<=s<II, k>=0)")
for op in op_ids:
    solver.assert_and_track(t[op] == k[op] * II + s[op], z3.Bool(f'mod_{op}'))
    solver.assert_and_track(s[op] >= 0, z3.Bool(f's_ge0_{op}'))
    solver.assert_and_track(s[op] < II,  z3.Bool(f's_lt_{op}'))
    solver.assert_and_track(k[op] >= 0,  z3.Bool(f'k_ge0_{op}'))
check(solver, "模调度线性化后")

# 2. RAW 因果依赖
print("\n[2] 添加 RAW 因果依赖约束")
for consumer, producers in depends_on.items():
    for prod in producers:
        lat = DELAYS[op_types[prod]]
        c = z3.Bool(f'raw_{prod}_{consumer}')
        solver.assert_and_track(t[consumer] >= t[prod] + lat, c)
check(solver, "RAW 约束后")

# 3. UR_READ 端口分配
print("\n[3] 添加 UR_READ 端口分配")
for op in ['read_a','read_b','read_c']:
    c = z3.Bool(f'iq_range_{op}')
    solver.assert_and_track(z3.Or([iq[op] == p for p in UR_READ_PORTS]), c)
check(solver, "端口分配后")

# 4. 读端口互斥（用 s_vars）
print("\n[4] 添加读端口 Slot 互斥（same_port -> s_i != s_j）")
reads = ['read_a','read_b','read_c']
for i in range(len(reads)):
    for j in range(i+1, len(reads)):
        op_i, op_j = reads[i], reads[j]
        same_port = (iq[op_i] == iq[op_j])
        c = z3.Bool(f'rport_tdm_{op_i}_{op_j}')
        solver.assert_and_track(z3.Implies(same_port, s[op_i] != s[op_j]), c)
check(solver, "读端口互斥后")

# 5. 防覆盖公理（anti-overwrite）
print("\n[5] 添加防覆盖公理（m_val - data_ready <= II）")
consumers_of = {op: [] for op in op_ids}
for consumer, producers in depends_on.items():
    for prod in producers:
        consumers_of[prod].append(consumer)

for op in op_ids:
    cons = consumers_of[op]
    if not cons:
        continue
    lat = DELAYS[op_types[op]]
    data_ready = t[op] + lat - 1
    m_val = t[cons[0]]
    for c_op in cons[1:]:
        m_val = z3.If(t[c_op] > m_val, t[c_op], m_val)
    c = z3.Bool(f'anti_ow_{op}')
    solver.assert_and_track(m_val - data_ready <= II, c)
check(solver, "防覆盖公理后")

# 6. 影子绑定（RTOVR -> ALU 窗口约束）
print("\n[6] 添加影子绑定（1 <= ALU - RTOVR_dep <= 4）")
rtovr_alu = {
    'mul_ab': ['rt_a', 'rt_b'],
    'add_c': ['rt_mul', 'rt_c']
}
for alu_op, rtovr_deps in rtovr_alu.items():
    for rd in rtovr_deps:
        c1 = z3.Bool(f'shadow_lo_{alu_op}_{rd}')
        c2 = z3.Bool(f'shadow_hi_{alu_op}_{rd}')
        solver.assert_and_track(t[alu_op] >= t[rd] + 1, c1)
        solver.assert_and_track(t[alu_op] <= t[rd] + 4, c2)
check(solver, "影子绑定后")

print("\n=== 诊断完成 ===")
if solver.check() == z3.sat:
    m = solver.model()
    print("\nII=1 解存在！各算子绝对发射时间：")
    for op in op_ids:
        print(f"  {op}: T={m[t[op]]}, s={m[s[op]]}, k={m[k[op]]}")
    for op in ['read_a','read_b','read_c']:
        print(f"  {op}: iq_port={m[iq[op]]}")
