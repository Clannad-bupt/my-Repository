import unittest

from api.hw_spec import HardwareSpec
from synthesis.ucode_solver import UCodeSynthesizer
from sim.tests.helpers import make_op

class TestRTOVRReject(unittest.TestCase):
    def setUp(self):
        self.spec = HardwareSpec.load("hw_config")
        self.synth = UCodeSynthesizer(self.spec)

    def test_rtovr_undersampling_reject(self):
        """测试 Verifier 是否能正确拒绝采样过早的调度"""
        prod = make_op("FPMUL", "prod")
        cons = make_op("FPADD", "cons", deps=["prod"])
        cons.metadata["rtovr_deps"] = {"prod": {"dvr": 1, "sel": 2}}
        
        ops = [prod, cons]
        unit_map = {"prod": "fpmul", "cons": "fpadd"}
        
        # 正常 FPMUL 延迟是 4
        # prod 发射在 0，准备好在 0 + 4 = 4
        # RTOVR 应该在 4 之后采样。
        # 如果 cons 发射在 4，RTOVR 采样推断为 3 ( < 4)，应该被拒绝。
        schedule = {"prod": 0, "cons": 4}
        
        result = self.synth.validate_physical_schedule(ops, schedule, unit_map, II=10)
        self.assertFalse(result, "Verifier 应当拒绝采样过早的非法调度")

    def test_rtovr_overwrite_reject(self):
        """测试 Verifier 是否能正确拒绝采样过晚的调度"""
        prod = make_op("FPMUL", "prod")
        cons = make_op("FPADD", "cons", deps=["prod"])
        cons.metadata["rtovr_deps"] = {"prod": {"dvr": 1, "sel": 2}}
        
        ops = [prod, cons]
        unit_map = {"prod": "fpmul", "cons": "fpadd"}
        
        II = 10
        # prod 发射在 0，在 0 + II = 10 时，被下一次迭代覆盖。
        # cons 发射在 11，RTOVR 采样推断为 10 ( >= 10)，此时数据已失效。
        schedule = {"prod": 0, "cons": 11}
        
        result = self.synth.validate_physical_schedule(ops, schedule, unit_map, II=II)
        self.assertFalse(result, "Verifier 应当拒绝采样过晚（已被覆盖）的非法调度")

if __name__ == '__main__':
    unittest.main()
