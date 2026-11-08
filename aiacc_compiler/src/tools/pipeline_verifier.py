import json
import re
from pathlib import Path
import numpy as np

class PipelineVerifier:
    """
    AIACC 标准化核心验证套件。
    用于在前端数学编译、时序排布和物理仿真各个阶段进行反向防错断言。
    """
    
    @staticmethod
    def verify_stage_1_dfg_math(dfg, input_data: dict, out_name: str):
        """
        [Stage 1] 前端 DFG 纯数字数学验证：
        抛开所有汇编与硬件时序，直接用 Python 浮点运算顺推整图。
        用于证明：从 sympy ast 到 DFG 的转换、常数处理等逻辑是完全正确的。
        """
        val_map = {}
        nodes_by_id = {n.trace_id: n for n in dfg}
        
        def eval_node(node_id):
            if node_id in val_map:
                return val_map[node_id]
            
            node = nodes_by_id[node_id]
            
            if node.op_type == "UR_READ":
                if node.metadata.get("is_const"):
                    val = float(node.metadata.get("value", 0.0))
                else:
                    name = node.metadata.get("meta_name", node_id)
                    val = float(input_data.get(name, 0.0)[0] if isinstance(input_data.get(name, 0.0), (list, np.ndarray)) else input_data.get(name, 0.0))
                val_map[node_id] = val
                return val
            
            if node.op_type == "RTOVR":
                deps = node.metadata.get("depends_on", [])
                val = eval_node(deps[0]) if deps else 0.0
                val_map[node_id] = val
                return val
                
            if node.op_type == "FPADD":
                deps = node.metadata.get("depends_on", [])
                v0 = eval_node(deps[0]) if len(deps) > 0 else 0.0
                v1 = eval_node(deps[1]) if len(deps) > 1 else 0.0
                is_sub = node.metadata.get("SUB", 0)
                val = v0 - v1 if is_sub else v0 + v1
                val_map[node_id] = val
                return val
                
            if node.op_type == "FPMUL":
                deps = node.metadata.get("depends_on", [])
                v0 = eval_node(deps[0]) if len(deps) > 0 else 0.0
                v1 = eval_node(deps[1]) if len(deps) > 1 else 0.0
                val = v0 * v1
                val_map[node_id] = val
                return val
                
            if node.op_type == "UR_WRITE":
                deps = node.metadata.get("depends_on", [])
                val = eval_node(deps[0]) if deps else 0.0
                val_map[node_id] = val
                return val
                
            val_map[node_id] = 0.0
            return 0.0

        print(f"\n🔍 [Stage 1 Verifier] Testing DFG Math using Input Data: {input_data}")
        # Run topo eval
        for node in dfg:
            eval_node(node.trace_id)
            
        final_val = None
        for node in dfg:
            if node.op_type == "UR_WRITE" and node.metadata.get("meta_name") == out_name:
                final_val = val_map[node.trace_id]
                break
                
        return val_map, final_val

    @staticmethod
    def verify_stage_2_liveness(dfg):
        """
        [Stage 2] SMT 时序排布的 Liveness 覆盖验证：
        查找如 FPMUL/FPADD 这样不具有自身状态保存能力的 ALU。
        如果它的上一次计算产出(T_prod)，在其某个消费者(RTOVR 真正读入数据, T_cons)到达之前，
        ALU 又执行了其他操作(T_exec)，意味着寄存器被脏写覆盖 (Liveness Corrupted)！
        """
        print(f"\n🔍 [Stage 2 Verifier] Auditing Hardware Registers Liveness...")
        nodes_by_id = {n.trace_id: n for n in dfg}
        
        # 记录 ALU 的全量执行时间线 (op_type -> [(cycle, trace_id)])
        alu_execs = {"FPMUL": [], "FPADD": []}
        for node in dfg:
            if node.op_type in alu_execs and hasattr(node, "logic_cycle"):
                alu_execs[node.op_type].append((node.logic_cycle, node.trace_id))
                
        for t in alu_execs:
            alu_execs[t].sort(key=lambda x: x[0])
            
        errors = []
        for node in dfg:
            if node.op_type in ["FPMUL", "FPADD"] and hasattr(node, "logic_cycle"):
                t_prod = node.logic_cycle
                
                # 寻找依赖这个 ALU 的消费方 (通常是 RTOVR 读取它)
                consumers = [n for n in dfg if node.trace_id in n.metadata.get("depends_on", [])]
                for cons in consumers:
                    t_cons = getattr(cons, "logic_cycle", -1)
                    if t_cons > t_prod:
                        # 检查在执行范围 (t_prod, t_cons) 内，是否有同样类型的 ALU 的其他实例运行
                        for exec_t, exec_id in alu_execs[node.op_type]:
                            if t_prod < exec_t < t_cons and exec_id != node.trace_id:
                                err = f"FATAL Liveness Corrupted: {node.trace_id} (T={t_prod}) -> {cons.trace_id} (T={t_cons}) is overwritten by {exec_id} at T={exec_t}"
                                errors.append((err, node.trace_id, t_prod, t_cons, exec_id, exec_t))
        return errors
