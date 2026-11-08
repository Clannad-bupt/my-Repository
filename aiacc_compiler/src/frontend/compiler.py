import sympy as sp
from typing import List, Dict, Any
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS
from frontend.optimizer import PhysicalGraphOptimizer

# 🛸 [V6.0] 引入算子库系统
from .kernels.registry import KernelRegistry
import frontend.kernels.standard_kernels # 确保标准算子已注册

class GraphCompiler:
    """
    AIACC 前端编译器 (V6.0 - 算子库解耦版)。
    主编译器现在仅负责图拓扑管理与资源分配，具体的数学转换逻辑已下沉至 kernels 目录。
    """
    def __init__(self, optimizer: Any = None):
        self.b = MIROpBuilder()
        self.mir_env = {}
        self.optimizer = optimizer
        
        # 预先定义每个算力单元在硬件中暴露出的目标插槽 (Slot)
        self.slot_map = {
            "FPMUL": [PS.FPMUL_S0, PS.FPMUL_S1],
            "FPADD": [PS.FPADD_S0, PS.FPADD_S1],
            "INTADD": [PS.INTADD_S0, PS.INTADD_S1, PS.INTADD_S2],
            "LOGIC": [PS.LOGIC_S0, PS.LOGIC_S1],
            "FPTOFP": [PS.FPTOFP_S],
            "INTTOINT": [PS.INTTOINT_S],
            "FPTOINT": [PS.FPTOINT_S],
            "INTTOFP": [PS.INTTOFP_S],
            "INT8TOFP16": [PS.INT8TOFP16_S0, PS.INT8TOFP16_S1],
            "UR_WRITE": [PS.UR_W0_S, PS.UR_W1_S, PS.UR_W2_S, PS.UR_W3_S]
        }
        self.rt_cnt = 0
        self.inl_cnt = 0
        self.cse_vars = set()

    def _route(self, nid: str, target_slot: PS, dep_port: str = "D"):
        """路由下发：在数学操作数进计算器前，必须包一层硬件专属搬运指令 RTOVR"""
        self.rt_cnt += 1
        uid = f"rt_{nid}_{target_slot.name}_{self.rt_cnt}"[:64]
        kwargs = {"dep_port": dep_port}
        
        # [Phase 4] 编译期证明 SEL：从 dr_map 直接推断 sel，杜绝装配期猜测
        if getattr(self, "spec", None) is not None and hasattr(self.spec, "get_dr_map"):
            producer = next((o for o in self.b.ops if o.trace_id == nid), None)
            if producer:
                src_type = str(producer.op_type).upper()
                dr_map = self.spec.get_dr_map()
                
                sel_id = 0
                if src_type in dr_map:
                    sel_id = dr_map[src_type].get(dep_port, dr_map[src_type].get("D", 0))
                elif src_type == "UR_READ":
                    # 获取该单元的真实 ID（例如从 ur-r2 的 metadata 获取）
                    iq_type_str = getattr(producer.metadata.get("iq_type", ""), "name", "")
                    if "UR_R" in iq_type_str:
                        idx = int(iq_type_str[-1]) if iq_type_str[-1].isdigit() else 0
                        base_ur = 30 # 通常 UR_READ_0 在 dr_map 中对应 base 30
                        unit_key = f"UR_READ_{idx}"
                        sel_id = dr_map.get(unit_key, {}).get(dep_port, base_ur + idx)
                kwargs["sel"] = sel_id

        return self.b.make_op("RTOVR", uid, target_slot, [nid], **kwargs)

    def compile_tensor(self, inputs, formula, shape: List[int], out_name="out_0") -> List:
        flat_size = 1
        for s in shape: flat_size *= s
        if flat_size % 4 != 0: raise ValueError(f"Tensor size {flat_size} must be 4-aligned")
        
        simd_blocks = flat_size // 4
        if len(shape) >= 2:
            inner_blocks = shape[-1] // 4
            self.loop_context = {
                "inner_loops": inner_blocks - 1,
                "outer_loops": shape[-2] - 1,
                "inner_inc": 1,
                "outer_inc": inner_blocks 
            }
        else:
            self.loop_context = {"inner_loops": simd_blocks - 1, "outer_loops": 0, "inner_inc": 1, "outer_inc": 0}
        
        dfg = self.compile(inputs, formula, out_name)
        for op in dfg:
            op.metadata.update({"shape": shape, **self.loop_context})
        return dfg

    def compile(self, inputs: Dict, formula: Any, out_name="out_0", spec=None) -> List[Any]:
        """
        编译主函数。将复杂的符号数学公式压缩、优化并装配为底层算子图。
        inputs: DFG 的输入口锚点，允许指派测试用的 kwargs (如 manual_bank)
        """
        # 🛸 [ADPC V4.0] 关键修复：重排时清空中间态环境，防止节点在多轮 Fuzz 中累积爆炸。
        from ir.mir.builder import MIROpBuilder
        self.b = MIROpBuilder()
        self.mir_env = {}
        self.cse_vars = set()
        self.const_pool = {} 
        self.spec = spec  # [Phase 4] 存储 spec 以供 _route 查表证明
        
        self.inputs_meta = inputs
        
        # 🛸 [ALGORITHM OPTIMIZATION] 支持多输出/矩阵算子（如 matmul）：自动解包并平铺为标量表达式列表进行公因子提取
        if hasattr(formula, "tolist"):
            formula_list = formula.tolist()
            import itertools
            if isinstance(formula_list[0], list):
                formula_list = list(itertools.chain(*formula_list))
            opt_nodes, final_res = sp.cse(formula_list)
        elif isinstance(formula, (list, tuple)):
            opt_nodes, final_res = sp.cse(formula)
        else:
            opt_nodes, final_res = sp.cse(formula)
        
        write_slots = self.slot_map["UR_WRITE"]
        num_w_ports = len(write_slots)

        for i, (sym, expr) in enumerate(opt_nodes):
            self.cse_vars.add(sym)
            self._lower_expr(sym, expr)
            # 🛸 [ENGINEERING] 移除此处的手动 Spill 逻辑，统一由 PhysicalGraphOptimizer 处理
            
        for i, res_expr in enumerate(final_res):
            res_sym = sp.Symbol(f"{out_name}_{i}")
            if res_expr not in self.mir_env: self._lower_expr(res_sym, res_expr)
            else: self.mir_env[res_sym] = self.mir_env[res_expr]
            
            write_slot = write_slots[i % num_w_ports]
            routed_res = self._route(self.mir_env[res_sym], write_slot)
            kwargs = {"role": "output", "meta_name": out_name}
            if hasattr(self, "loop_context"): kwargs["inc"] = self.loop_context["inner_inc"]
            self.b.make_op("UR_WRITE", f"out_obj_{i}", RT.UR_WRITE, [routed_res], **kwargs)
            
        # --- Stage 3: 物理图优化与生命周期加固 (Engineering Grade) ---
        # 🛸 [ENGINEERING] 动态获取硬件参数，防止寻址塌缩
        bank_depth = 256
        scratch_bank = 6
        if hasattr(spec, "params"):
            bank_depth = getattr(spec.params, "PARAM_UR_BANK_DEPTH", 256)
            bank_num = getattr(spec.params, "PARAM_UR_BANK_NUM", 8)
            scratch_bank = bank_num - 2 if bank_num > 2 else 0
            
        if getattr(self, "optimizer", None) is not None:
            optimizer = self.optimizer
        else:
            optimizer = PhysicalGraphOptimizer(scratch_bank=scratch_bank, bank_depth=bank_depth)
        optimized_dfg = optimizer.optimize(self.b.get_dfg())
        
        return optimized_dfg

    def _lower_expr(self, sym, expr):
        """
        [ADPC V6.0] 内核化下沉入口。
        """
        if isinstance(expr, sp.Symbol):
            self.mir_env[sym] = self._get_op(expr)
            return
        if isinstance(expr, (sp.Float, sp.Integer, sp.Rational)):
            self.mir_env[sym] = self._get_const(expr)
            return

        lower_func = KernelRegistry.lookup(expr)
        if lower_func:
            lower_func(self, sym, expr)
            return

        raise NotImplementedError(f"Compiler Error: 未在算子库中找到匹配内核 {type(expr)}: {expr}")

    def _get_op(self, arg):
        # 🛸 [V6.2 FIX] CSE 变量现在统一且自动由 PhysicalGraphOptimizer 进行全局生命周期审计与溢出/重载 (Spill-Reload) 变换
        # 编译器前端仅需返回 mir_env 中的原始节点，构建纯净的数据流图
        if arg in self.mir_env: return self.mir_env[arg]
        if arg in self.inputs_meta or str(arg) in self.inputs_meta: return self._get_input(arg)
        
        if isinstance(arg, sp.Symbol): raise NameError(f"Unbound: {arg}")
        if isinstance(arg, (sp.Float, sp.Integer, sp.Rational)): return self._get_const(arg)
        
        self.inl_cnt += 1
        inline_sym = sp.Symbol(f"inl_{self.inl_cnt}")
        self._lower_expr(inline_sym, arg)
        return self.mir_env[inline_sym]

    def _get_const(self, val):
        val_f = float(val)
        if val_f in self.const_pool: return self.const_pool[val_f]
        nid = f"cinl_{len(self.const_pool)}_{val_f}".replace(".", "_").replace("-", "n")
        metadata = self.inputs_meta.get("constants", {}).copy()
        
        # 🛸 关键优化：常量读取通常设置 INC=0，因为所有循环迭代共用同一个常量地址
        if hasattr(self, "loop_context"):
            metadata["inc"] = 0 
            
        # 🛸 [ALGORITHM OPTIMIZATION] 算法专家级多 Bank 自动平摊：
        # 支持 YAML 显式指定常量分 Bank (logical_addr_list)；
        # 若未指定，则默认采用 Bank 7 和 Bank 6 自动交替分配，消除同一 Bank 1 读端口冲突瓶颈。
        if "logical_addr_list" in metadata:
            addr_list = metadata["logical_addr_list"]
            idx = len(self.const_pool)
            bank_idx = idx % len(addr_list)
            addr_offset = idx // len(addr_list)
            metadata["logical_addr"] = addr_list[bank_idx] + addr_offset
        elif "logical_addr" in metadata:
            base_addr = metadata["logical_addr"]
            idx = len(self.const_pool)
            
            # 动态获取硬件 Bank 深度，默认 256
            depth = 256
            if hasattr(self, "spec") and self.spec is not None and hasattr(self.spec, "params"):
                depth = getattr(self.spec.params, "PARAM_UR_BANK_DEPTH", 256)
            
            # 使用交替分配，即便只有 2 个常量也能平摊到不同 Bank，极大降低 Bank 冲突概率
            bank_offset = idx % 2
            addr_offset = idx // 2
            
            # 从 base_addr (通常是 Bank 7) 向下交替分配到 Bank 7 和 Bank 6
            metadata["logical_addr"] = (base_addr - bank_offset * depth) + addr_offset
            
        # 🛸 关键修复：防止重复传递 meta_name 和 role
        m_name = metadata.pop("meta_name", "constants")
        metadata.pop("role", None) # 统一在 make_op 中或 metadata 中指定
        node = self.b.make_op("UR_READ", nid, RT.UR_READ, [], role="constant", meta_name=m_name, **metadata)
        self.const_pool[val_f] = node
        return node

    def _get_input(self, sym):
        self.inl_cnt += 1
        new_id = f"{sym}_read_{self.inl_cnt}"
        kwargs = self.inputs_meta.get(sym, self.inputs_meta.get(str(sym), {})).copy()
        if hasattr(self, "loop_context"): kwargs["inc"] = self.loop_context["inner_inc"]
        m_name = kwargs.pop("meta_name", str(sym))
        return self.b.make_op("UR_READ", new_id, RT.UR_READ, [], meta_name=m_name, **kwargs)
