import sympy as sp
from .registry import KernelRegistry
from driver.hw_resource import ResourceType as RT, PortSlot as PS

# --- 1. 基础算术内核 (Mul/Add/Pow) ---

@KernelRegistry.register(lambda e: isinstance(e, sp.Mul))
def lower_mul(c, sym, expr):
    args = list(expr.args)
    if len(args) == 2:
        r0 = c._route(c._get_op(args[0]), PS.FPMUL_S0)
        r1 = c._route(c._get_op(args[1]), PS.FPMUL_S1)
        c.mir_env[sym] = c.b.make_op("FPMUL", str(sym), RT.FPMUL, [r0, r1])
    else:
        current = c._get_op(args[0])
        for i in range(1, len(args)):
            r0 = c._route(current, PS.FPMUL_S0)
            r1 = c._route(c._get_op(args[i]), PS.FPMUL_S1)
            current = c.b.make_op("FPMUL", f"{sym}_m{i}", RT.FPMUL, [r0, r1])
        c.mir_env[sym] = current

@KernelRegistry.register(lambda e: isinstance(e, sp.Add))
def lower_add(c, sym, expr):
    args = list(expr.args)
    op_list = [c._get_op(arg) for arg in args]
    current = op_list[0]
    for i in range(1, len(op_list)):
        r0 = c._route(current, PS.FPADD_S0)
        r1 = c._route(op_list[i], PS.FPADD_S1)
        current = c.b.make_op("FPADD", f"{sym}_a{i}", RT.FPADD, [r0, r1])
    c.mir_env[sym] = current

@KernelRegistry.register(lambda e: isinstance(e, sp.Add) and len(e.args) == 4 and all(isinstance(arg, sp.Mul) for arg in e.args))
def lower_fp32addtree_5to1(c, sym, expr):
    """
    [ALGORITHM OPTIMIZATION] 乘加树硬件高度流水化重构：
    不再采用高延迟、无法重叠的串行 FP32ADDTREE5TO1 节点链（这会导致 II = 16 性能极低）。
    相反，我们利用高吞吐、完全流水的 FPADD 浮点加法器，在 DFG 层面并行构建二叉求和树：
      sum0 = m0 + m1 (FPADD)
      sum1 = m2 + m3 (FPADD)
      sum_all = sum0 + sum1 (FPADD)
    最后将单路 4-lane 结果送入单个 FP32ADDTREE5TO1 节点，仅进行一次单拍规约，无状态且无循环携带依赖。
    这样不仅彻底排除了跨迭代的物理污染与 Latch 覆盖，还能让 SMT 求解器流畅求解出最优的 II = 4 计算吞吐！
    """
    args = list(expr.args)
    mul_nodes = [c._get_op(arg) for arg in args]

    # 1. 第一层并行 FPADD
    r_m0 = c._route(mul_nodes[0], PS.FPADD_S0)
    r_m1 = c._route(mul_nodes[1], PS.FPADD_S1)
    sum0 = c.b.make_op("FPADD", f"{sym}_sum0", RT.FPADD, [r_m0, r_m1])

    r_m2 = c._route(mul_nodes[2], PS.FPADD_S0)
    r_m3 = c._route(mul_nodes[3], PS.FPADD_S1)
    sum1 = c.b.make_op("FPADD", f"{sym}_sum1", RT.FPADD, [r_m2, r_m3])

    # 2. 第二层 FPADD 汇聚成一个 4-lane vector
    r_s0 = c._route(sum0, PS.FPADD_S0)
    r_s1 = c._route(sum1, PS.FPADD_S1)
    sum_all = c.b.make_op("FPADD", f"{sym}_sum_all", RT.FPADD, [r_s0, r_s1])

    # 3. 单路规约求和
    r_final = c._route(sum_all, PS.FP32ADDTREE5TO1_S0)
    
    curr_node = c.b.make_op(
        "FP32ADDTREE5TO1",
        str(sym),
        RT.FP32ADDTREE5TO1,
        [r_final],
        cumulative=False,
        reduction_per_channel=4
    )
    c.mir_env[sym] = curr_node


@KernelRegistry.register(lambda e: isinstance(e, sp.Pow))
def lower_pow(c, sym, expr):
    base, exp = expr.args
    if isinstance(exp, sp.Integer):
        exp_val = int(exp)
        if exp_val > 0:
            current = c._get_op(base)
            for i in range(1, exp_val):
                r0 = c._route(current, PS.FPMUL_S0)
                r1 = c._route(c._get_op(base), PS.FPMUL_S1)
                current = c.b.make_op("FPMUL", f"{sym}_pw{i}", RT.FPMUL, [r0, r1])
            c.mir_env[sym] = current
        elif exp_val == -1:
            # 🛸 [ADPC V4.0] 倒数泰勒展开优化版
            x_node = c._get_op(base)
            one_node = c._get_const(sp.Float(1.0))
            r_x, r_o1 = c._route(x_node, PS.FPADD_S0), c._route(one_node, PS.FPADD_S1)
            dx = c.b.make_op("FPADD", f"{sym}_dx", RT.FPADD, [r_x, r_o1], SUB=1)
            r_o2, r_dx1 = c._route(one_node, PS.FPADD_S0), c._route(dx, PS.FPADD_S1)
            y = c.b.make_op("FPADD", f"{sym}_y1", RT.FPADD, [r_o2, r_dx1], SUB=1)
            curr_dx_pow = dx
            for i in range(6):
                r_p0, r_p1 = c._route(curr_dx_pow, PS.FPMUL_S0), c._route(curr_dx_pow, PS.FPMUL_S1)
                curr_dx_pow = c.b.make_op("FPMUL", f"{sym}_p{i}", RT.FPMUL, [r_p0, r_p1])
                r_o, r_p = c._route(one_node, PS.FPADD_S0), c._route(curr_dx_pow, PS.FPADD_S1)
                term = c.b.make_op("FPADD", f"{sym}_t{i}", RT.FPADD, [r_o, r_p])
                r_y, r_t = c._route(y, PS.FPMUL_S0), c._route(term, PS.FPMUL_S1)
                y = c.b.make_op("FPMUL", f"{sym}_res{i}", RT.FPMUL, [r_y, r_t])
            c.mir_env[sym] = y

# --- 2. 激活函数与对比内核 (Max/Min/Abs/Relational) ---

@KernelRegistry.register(lambda e: isinstance(e, (sp.Max, sp.Min)))
def lower_minmax(c, sym, expr):
    args = list(expr.args)
    if len(args) == 2:
        a, b = c._get_op(args[0]), c._get_op(args[1])
        r0_cmp, r1_cmp = c._route(a, PS.FPADD_S0), c._route(b, PS.FPADD_S1)
        cmp = c.b.make_op("FPADD", f"{sym}_cmp", RT.FPADD, [r0_cmp, r1_cmp], UPDATE_ST=1, SUB=1)
        st_wire = c._route(cmp, PS.LOGIC_ST, dep_port="ST")
        r0_sel, r1_sel = c._route(a, PS.LOGIC_S0), c._route(b, PS.LOGIC_S1)
        op_type = "SELECT_GT" if isinstance(expr, sp.Max) else "SELECT_LS"
        c.mir_env[sym] = c.b.make_op("LOGIC", str(sym), RT.LOGIC, [r0_sel, r1_sel, st_wire], OP_TYPE=op_type, VALID=1)

@KernelRegistry.register(lambda e: isinstance(e, sp.Abs))
def lower_abs(c, sym, expr):
    c.mir_env[sym] = c._get_op(sp.Max(expr.args[0], -expr.args[0]))

@KernelRegistry.register(lambda e: isinstance(e, (sp.GreaterThan, sp.LessThan, sp.StrictGreaterThan, sp.StrictLessThan, sp.Equality, sp.Unequality)))
def lower_comparison(c, sym, expr):
    a, b = c._get_op(expr.args[0]), c._get_op(expr.args[1])
    r0_cmp, r1_cmp = c._route(a, PS.FPADD_S0), c._route(b, PS.FPADD_S1)
    cmp = c.b.make_op("FPADD", f"{sym}_cmp", RT.FPADD, [r0_cmp, r1_cmp], UPDATE_ST=1, SUB=1)
    st_wire = c._route(cmp, PS.LOGIC_ST, dep_port="ST")
    one, zero = c._get_const(1.0), c._get_const(0.0)
    
    # 默认配置
    op_type, r0, r1 = "SELECT_GT", one, zero
    if isinstance(expr, sp.StrictGreaterThan): op_type = "SELECT_GT"
    elif isinstance(expr, sp.GreaterThan): op_type, r0, r1 = "SELECT_LS", zero, one
    elif isinstance(expr, sp.StrictLessThan): op_type = "SELECT_LS"
    elif isinstance(expr, sp.LessThan): op_type, r0, r1 = "SELECT_GT", zero, one
    elif isinstance(expr, (sp.Eq, sp.Equality)): op_type = "SELECT_EQ"
    elif isinstance(expr, (sp.Ne, sp.Unequality)): op_type, r0, r1 = "SELECT_EQ", zero, one
    
    c.mir_env[sym] = c.b.make_op("LOGIC", str(sym), RT.LOGIC, [c._route(r0, PS.LOGIC_S0), c._route(r1, PS.LOGIC_S1), st_wire], OP_TYPE=op_type, VALID=1)

@KernelRegistry.register(lambda e: isinstance(e, sp.Piecewise))
def lower_piecewise(c, sym, expr):
    # args 格式: [(val1, cond1), (val2, cond2), ..., (val_n, True)]
    args = expr.args
    if len(args) < 2 or args[-1][1] != True:
        raise ValueError(f"Piecewise must have at least 2 branches and end with (val, True): {expr}")
    
    # 从后往前构建选择链 (MUX Chain)
    # 初始默认值
    current_op = c._get_op(args[-1][0])
    
    for i in range(len(args) - 2, -1, -1):
        val_expr, cond_expr = args[i]
        
        # 1. 解析比较条件
        if isinstance(cond_expr, sp.StrictGreaterThan):
            a, b = cond_expr.args
            op_type = "SELECT_GT"
        elif isinstance(cond_expr, sp.GreaterThan):
            a, b = cond_expr.args
            op_type = "SELECT_GE"
        elif isinstance(cond_expr, sp.StrictLessThan):
            a, b = cond_expr.args
            op_type = "SELECT_LS"
        elif isinstance(cond_expr, sp.LessThan):
            a, b = cond_expr.args
            op_type = "SELECT_LE"
        elif isinstance(cond_expr, sp.Eq):
            a, b = cond_expr.args
            op_type = "SELECT_EQ"
        else:
            raise ValueError(f"Unsupported condition in Piecewise: {cond_expr}")
        
        # 2. 生成状态对比节点 (a - b)
        op_a = c._get_op(a)
        op_b = c._get_op(b)
        r0_cmp = c._route(op_a, PS.FPADD_S0)
        r1_cmp = c._route(op_b, PS.FPADD_S1)
        cmp_node = c.b.make_op("FPADD", f"{sym}_cmp_{i}", RT.FPADD, [r0_cmp, r1_cmp], 
                                  UPDATE_ST=1, SUB=1)
        
        # 3. 生成逻辑选择节点 (IF cond_i THEN val_i ELSE current_op)
        op_v1 = c._get_op(val_expr)
        st_wire = c._route(cmp_node, PS.LOGIC_ST, dep_port="ST")
        r0_sel = c._route(op_v1, PS.LOGIC_S0)
        r1_sel = c._route(current_op, PS.LOGIC_S1)
        
        current_op = c.b.make_op("LOGIC", f"{sym}_mux_{i}", RT.LOGIC, 
                                    [r0_sel, r1_sel, st_wire], 
                                    OP_TYPE=op_type, VALID=1)
    
    c.mir_env[sym] = current_op

# --- 3. 舍入内核 (floor, ceiling, round) ---

@KernelRegistry.register(lambda e: isinstance(e, (sp.floor, sp.ceiling)))
def lower_rounding(c, sym, expr):
    x, one = c._get_op(expr.args[0]), c._get_const(1.0)
    i32 = c.b.make_op("FPTOINT", f"{sym}_trunc", RT.FPTOINT, [c._route(x, PS.FPTOINT_S)])
    f_trunc = c.b.make_op("INTTOFP", f"{sym}_recept", RT.INTTOFP, [c._route(i32, PS.INTTOFP_S)])
    cmp = c.b.make_op("FPADD", f"{sym}_cmp", RT.FPADD, [c._route(f_trunc, PS.FPADD_S0), c._route(x, PS.FPADD_S1)], UPDATE_ST=1, SUB=1)
    st_wire = c._route(cmp, PS.LOGIC_ST, dep_port="ST")
    
    if isinstance(expr, sp.floor):
        f_adj = c.b.make_op("FPADD", f"{sym}_minus1", RT.FPADD, [c._route(f_trunc, PS.FPADD_S0), c._route(one, PS.FPADD_S1)], SUB=1)
        c.mir_env[sym] = c.b.make_op("LOGIC", str(sym), RT.LOGIC, [c._route(f_adj, PS.LOGIC_S0), c._route(f_trunc, PS.LOGIC_S1), st_wire], OP_TYPE="SELECT_GT", VALID=1)
    else:
        f_adj = c.b.make_op("FPADD", f"{sym}_plus1", RT.FPADD, [c._route(f_trunc, PS.FPADD_S0), c._route(one, PS.FPADD_S1)], SUB=0)
        c.mir_env[sym] = c.b.make_op("LOGIC", str(sym), RT.LOGIC, [c._route(f_adj, PS.LOGIC_S0), c._route(f_trunc, PS.LOGIC_S1), st_wire], OP_TYPE="SELECT_LS", VALID=1)

# --- 4. 硬件函数直接映射 ---

@KernelRegistry.register(lambda e: isinstance(e, sp.Function) and type(e) not in (sp.Abs, sp.Max, sp.Min, sp.floor, sp.ceiling, sp.Piecewise))
def lower_function(c, sym, expr):
    func_name = str(expr.func)
    if hasattr(RT, func_name):
        rt = getattr(RT, func_name)
        slots = c.slot_map.get(func_name, [PS.FPMUL_S0, PS.FPMUL_S1]) 
        routed_args = [c._route(c._get_op(arg), slots[i]) for i, arg in enumerate(expr.args)]
        c.mir_env[sym] = c.b.make_op(func_name, str(sym), rt, routed_args)
    else:
        # Fallback: check if we have a custom kernel for this function name
        raise NotImplementedError(f"Kernel Library: 不支持的硬件函数 {func_name}")
