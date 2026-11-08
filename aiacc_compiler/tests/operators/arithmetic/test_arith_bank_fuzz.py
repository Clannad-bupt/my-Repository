import numpy as np
import pytest
import itertools
from api.op_tester import OperatorPhysicalTester
from library.atomic.arithmetic.basic_ops import get_add_dfg, get_mul_dfg, get_reciprocal_dfg
from library.atomic.activations.relu import get_relu_dfg
from library.atomic.transcendentals.sin import get_sin_8order_dfg
from library.atomic.transcendentals.cos import get_cos_8order_dfg
from tests.operators.utils.data_generator import ArithTestDataGenerator

class TestArithmeticBankRobustness:
    """
    基础算子物理鲁棒性测试 (ADPC 标准版)。
    [V3.2 更新]: 纳入 Sin 8阶泰勒展开验证。
    """

    @pytest.mark.parametrize("op_type", ["add", "sub", "mul", "rec", "relu", "sin", "cos", "exp", "div", "neg", "and", "or", "xor", "not", "sqrt", "rsqrt", "select_gt", "select_eq", "select_ls", "int8tofp16", "atan2"])
    @pytest.mark.parametrize("data_mode", ["fuzz", "boundary"])
    def test_arithmetic_ccug_fuzzing(self, op_type, data_mode):
        """对基础算子执行 CCUG 角色自适应压力测试"""
        tester = OperatorPhysicalTester()
        
        # 1. 算法选择与角色对应的参考模型
        from library.atomic.arithmetic.basic_ops import get_add_dfg, get_sub_dfg, get_mul_dfg, get_reciprocal_dfg, get_neg_dfg
        from library.atomic.activations.relu import get_relu_dfg
        from library.atomic.transcendentals.sin import get_sin_8order_dfg
        from library.atomic.transcendentals.cos import get_cos_8order_dfg
        from library.atomic.transcendentals.exp import get_exp_8order_dfg
        from library.atomic.arithmetic.div import get_div_dfg
        from library.atomic.logical.logical_ops import get_and_dfg, get_or_dfg, get_xor_dfg, get_not_dfg
        from library.atomic.powers.sqrt import get_sqrt_v4_dfg
        from library.atomic.powers.rsqrt import get_rsqrt_v4_dfg
        from library.atomic.logical.logical_ops import get_select_gt_dfg, get_select_eq_dfg, get_select_ls_dfg
        from library.atomic.conversions.quant_ops import get_int8_to_fp16_dfg
        from library.atomic.transcendentals.atan2 import get_atan2_v4_dfg

        if op_type == "add":
            dfg_ops = get_add_dfg()
            role_to_input = {"operand_a": "x_val", "operand_b": "y_val"}
            ref_fn = lambda d: d["operand_a"] + d["operand_b"]
        elif op_type == "sub":
            dfg_ops = get_sub_dfg()
            role_to_input = {"operand_a": "x_val", "operand_b": "y_val"}
            ref_fn = lambda d: d["operand_a"] - d["operand_b"]
        elif op_type == "neg":
            dfg_ops = get_neg_dfg()
            role_to_input = {"operand_a": "x_val"}
            ref_fn = lambda d: -d["operand_a"]
        elif op_type == "mul":
            dfg_ops = get_mul_dfg()
            role_to_input = {"operand_a": "x_val", "operand_b": "y_val"}
            ref_fn = lambda d: d["operand_a"] * d["operand_b"]
        elif op_type == "rec":
            dfg_ops = get_reciprocal_dfg()
            role_to_input = {"operand_a": "x_val", "initial_guess": "y0_val"}
            ref_fn = lambda d: d["initial_guess"] * (2.0 - d["operand_a"] * d["initial_guess"])
        elif op_type == "relu":
            dfg_ops = get_relu_dfg()
            role_to_input = {"operand_a": "x_val"}
            ref_fn = lambda d: np.where(np.isnan(d["operand_a"]), 0.0, np.maximum(0, d["operand_a"]))
        elif op_type == "sin":
            dfg_ops = get_sin_8order_dfg()
            role_to_input = {"operand_a": "x_val"}
            ref_fn = lambda d: np.sin(d["operand_a"])
        elif op_type == "cos":
            dfg_ops = get_cos_8order_dfg()
            role_to_input = {"operand_a": "x_val"}
            ref_fn = lambda d: np.cos(d["operand_a"])
        elif op_type == "exp":
            dfg_ops = get_exp_8order_dfg()
            role_to_input = {"operand_a": "x_val"}
            ref_fn = lambda d: np.exp(d["operand_a"])
        elif op_type == "div":
            dfg_ops = get_div_dfg()
            role_to_input = {"operand_a": "x_val", "operand_b": "y_val", "initial_guess": "y0_val"}
            ref_fn = lambda d: d["operand_a"] / d["operand_b"]
        elif op_type == "and":
            dfg_ops = get_and_dfg()
            role_to_input = {"operand_a": "x_val", "operand_b": "y_val"}
            ref_fn = lambda d: (d["operand_a"].view(np.uint32) & d["operand_b"].view(np.uint32)).view(np.float32)
        elif op_type == "or":
            dfg_ops = get_or_dfg()
            role_to_input = {"operand_a": "x_val", "operand_b": "y_val"}
            ref_fn = lambda d: (d["operand_a"].view(np.uint32) | d["operand_b"].view(np.uint32)).view(np.float32)
        elif op_type == "xor":
            dfg_ops = get_xor_dfg()
            role_to_input = {"operand_a": "x_val", "operand_b": "y_val"}
            ref_fn = lambda d: (d["operand_a"].view(np.uint32) ^ d["operand_b"].view(np.uint32)).view(np.float32)
        elif op_type == "not":
            dfg_ops = get_not_dfg()
            role_to_input = {"operand_a": "x_val"}
            ref_fn = lambda d: (~d["operand_a"].view(np.uint32)).view(np.float32)
        elif op_type == "sqrt":
            dfg_ops = get_sqrt_v4_dfg()
            role_to_input = {"operand_a": "x_val", "initial_guess": "y0_val"}
            # Sqrt(x) 1-iteration Newton reference: x * [0.5 * y0 * (3.0 - x * y0^2)]
            ref_fn = lambda d: d["operand_a"] * (0.5 * d["initial_guess"] * (3.0 - d["operand_a"] * d["initial_guess"]**2))
        elif op_type == "rsqrt":
            dfg_ops = get_rsqrt_v4_dfg()
            role_to_input = {"operand_a": "x_val", "initial_guess": "y0_val"}
            # Rsqrt(x) 1-iteration Newton reference: 0.5 * y0 * (3.0 - x * y0^2)
            ref_fn = lambda d: 0.5 * d["initial_guess"] * (3.0 - d["operand_a"] * d["initial_guess"]**2)
        elif op_type.startswith("select"):
            if "gt" in op_type:
                dfg_ops = get_select_gt_dfg()
                check_bit = 4
            elif "eq" in op_type:
                dfg_ops = get_select_eq_dfg()
                check_bit = 2
            else: # ls
                dfg_ops = get_select_ls_dfg()
                check_bit = 1
            role_to_input = {"operand_a": "x_val", "operand_b": "y_val", "condition": "st_val"}
            # Ref: out = a if (st & check_bit) else b
            ref_fn = lambda d: np.where(d["condition"].view(np.uint32) & check_bit, d["operand_a"], d["operand_b"])
        elif op_type == "int8tofp16":
            dfg_ops = get_int8_to_fp16_dfg()
            role_to_input = {"operand_a": "i8_lo", "operand_b": "i8_hi"}
            def int8_to_fp16_ref(d):
                import struct
                s0_raw = d["operand_a"].view(np.uint32)
                s1_raw = d["operand_b"].view(np.uint32)
                n_rows = s0_raw.shape[0]
                res = {f"f16_res_{k}": [] for k in range(4)}
                for r in range(n_rows):
                    s0_word = sum(int(s0_raw[r, k]) << (32 * k) for k in range(4))
                    s1_word = sum(int(s1_raw[r, k]) << (32 * k) for k in range(4))
                    d_words = [0]*4
                    for i in range(32):
                        val_i8 = ((s1_word >> (i*4)) & 0xF) << 4 | (s0_word >> (i*4)) & 0xF
                        if val_i8 >= 128: val_i8 -= 256
                        f16_bits = struct.unpack('<H', struct.pack('<e', float(val_i8)))[0]
                        for k in range(4): d_words[k] |= ((f16_bits >> (k*4)) & 0xF) << (i*4)
                    for k in range(4):
                        res[f"f16_res_{k}"].append(np.array([(d_words[k] >> (j*32)) & 0xFFFFFFFF for j in range(4)], dtype=np.uint32).view(np.float32))
                return {k: np.array(v) for k, v in res.items()}
            ref_fn = int8_to_fp16_ref
        elif op_type == "atan2":
            dfg_ops = get_atan2_v4_dfg()
            role_to_input = {"operand_a": "y_val", "operand_b": "x_val", "initial_guess": "y0_val"}
            ref_fn = lambda d: np.arctan2(d["operand_a"], d["operand_b"])
        else:
            raise ValueError(f"Unknown op_type: {op_type}")

        # 2. 扫描 DFG 提取语义映射 (支持一角色多节点)
        # 🎉 支持 V3.3 物理保真 DFG：同一个 role 可能对应多个 UR_READ
        role_to_names = {} 
        ccug_nodes = []
        for op in dfg_ops:
            role = op.metadata.get("role")
            if role:
                if role not in role_to_names:
                    role_to_names[role] = []
                role_to_names[role].append(op.trace_id)
                ccug_nodes.append(op.trace_id)
            elif op.metadata.get("is_const"):
                # 常量也作为冲突源节点
                ccug_nodes.append(op.trace_id)

        # 3. 产出物理独立的多通道测试数据
        raw_data = ArithTestDataGenerator.generate(op_type, mode=data_mode, size=4)
        
        # 4. 角色 -> 物理节点 -> 数据通道映射
        input_data = {}
        role_data = {} # 用于 ref_fn 的计算
        
        # 建立 role <-> data_channel 的标准映射关系
        role_data_channel_map = {
            "operand_a": "ch_0",
            "operand_b": "ch_1",
            "initial_guess": "ch_1", # 默认 rec 映射 (1/X)
            "condition": "ch_2"
        }
        
        if op_type in ["div", "atan2"]:
            # 🛸 特殊处理：x -> ch0, y -> ch1, y0 -> ch2
            role_data_channel_map["initial_guess"] = "ch_2"
        
        # 🛸 核心修复：遍历所有节点，确保所有 shared-role 节点都喂入相同数据
        for role, p_names in role_to_names.items():
            ch_key = role_data_channel_map.get(role)
            if ch_key in raw_data:
                val = raw_data[ch_key]
                role_data[role] = val
                for p_name in p_names:
                    input_data[p_name] = val

        # 5. 自动化常量提取
        constants = {}
        for op in dfg_ops:
            if op.metadata.get("is_const"):
                constants[op.trace_id] = float(op.metadata.get("value", 0.0))
        
        # 6. 动态计算预期值 (角色驱动)
        with np.errstate(divide='ignore', invalid='ignore'):
            golden_y = ref_fn(role_data)
        
        # 7. 生成 CCUG 冲突组合 (2^K)
        # 🛸 对于 Exp 等深层算子，K 可能很大。我们将其限制在 64 圈以内以防超时。
        combinations = list(itertools.product([0, 1], repeat=len(ccug_nodes)))
        if len(combinations) > 64:
            import random
            random.seed(42) # 保证测试可重现
            random.shuffle(combinations)
            combinations = combinations[:64]
        print(f"\n🚀 [ADPC-ST-V3] CCUG ({data_mode}) | OP: {op_type.upper()} | NameMap: {role_to_names}")

        for i, config_tuple in enumerate(combinations):
            static_banks = {node: config_tuple[j] for j, node in enumerate(ccug_nodes)}
            
            success, msg = tester.run_full_stack_verification(
                op_name=f"arith/{op_type}/{data_mode}_ccug_{i}",
                dfg_ops=dfg_ops,
                input_data=input_data,
                golden_y=golden_y,
                constants=constants,
                static_banks=static_banks,
                strategies=["balanced"] 
            )
            
            assert success, f"🚨 {op_type} CCUG 失败: {msg}"

if __name__ == "__main__":
    pytest.main([__file__, "-v", "-s"])
