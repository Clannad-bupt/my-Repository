import unittest
import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec
from synthesis.backend import SMTSynthesizer
from synthesis.regression_manager import RegressionManager
from library.atomic.transcendentals.sin import get_sin_8order_dfg

class TestOperatorStability(unittest.TestCase):
    """
    AIACC 稳定性回归全家桶。
    任何对 axioms.py 的更改都必须通过本测试。
    """
    def setUp(self):
        self.spec = HardwareSpec.load("hw_config")
        self.synthesizer = SMTSynthesizer(self.spec)
        self.reg_mgr = RegressionManager()

    def run_operator(self, name, dfg_func):
        """通用核心：对单个算子进行双模校验"""
        ops = dfg_func()
        
        for mode in ["stable", "extreme"]:
            with self.subTest(mode=mode):
                print(f"Running Stability Check: {name} in {mode} mode...")
                result = self.synthesizer.solve(ops, strategy=mode)
                
                # 基准校验 1: 必须有解 (SAT) - 如果连 SAT 都没了，公理层就写崩了
                self.assertIsNotNone(result, f"❌ FATAL: {name} in {mode} is now UNSAT. Axiom regression detected!")
                
                # 基准校验 2: 对比黄金快照 (如果存在) - 防止关键指标退化
                is_valid = self.reg_mgr.verify(name, mode, result)
                self.assertTrue(is_valid, f"❌ Stability: {name} in {mode} failed snapshot verification.")
                
                # [可选] 自动更新基准 (仅当手动确认性能提升时开启)
                # self.reg_mgr.save_golden(name, mode, result)

    def test_sin_8order(self):
        """测试正弦 (8阶泰勒) 稳定性"""
        self.run_operator("sin_8order", get_sin_8order_dfg)

    # 后续算子注册...
    # def test_exp_horner(self): ...
    # def test_log_newton(self): ...

if __name__ == "__main__":
    unittest.main()
