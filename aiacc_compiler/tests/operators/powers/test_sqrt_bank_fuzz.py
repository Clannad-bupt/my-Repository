import pytest
import itertools
import numpy as np
from src.library.atomic.powers.precision_roots import get_sqrt_industrial_dfg
from src.api.op_tester import OperatorPhysicalTester

class TestSqrtBankRobustness:
    """
    Sqrt 算子 Bank 冲突扫射
    验证 x_in, y0_seed, const_1_5 三路读取在极度拥挤下的处理。
    """
    def test_sqrt_ccug_fuzzing(self):
        tester = OperatorPhysicalTester()
        dfg_ops = get_sqrt_industrial_dfg()
        
        # 识别并发冲突团 (3路读取)
        ccug_nodes = ["x_in", "y0_seed", "const_1_5"]
        combinations = list(itertools.product([0, 1, 2], repeat=len(ccug_nodes))) # 三个 Bank 间扫射
        print(f"\n🚀 [SQRT-FUZZ] Starting {len(combinations)} cases for Sqrt (Newton).")
        
        input_data = {"x_in": [16.0]*4}
        golden_y = [4.0]*4
        constants = {"y0_seed": 0.25, "const_1_5": 1.5}

        for i, config_tuple in enumerate(combinations):
            static_banks = {node: config_tuple[j] for j, node in enumerate(ccug_nodes)}
            success, msg = tester.run_full_stack_verification(
                op_name=f"powers/sqrt/fuzz_{i}",
                dfg_ops=dfg_ops,
                input_data=input_data,
                golden_y=golden_y,
                constants=constants,
                static_banks=static_banks,
                strategies=["balanced"]
            )
            assert success, f"🚨 Sqrt Bank 冲突验证失败 [Case {i}]"
        print("✅ [PASS] Sqrt is robust against all bank combinations.")
