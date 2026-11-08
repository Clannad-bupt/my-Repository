import sys
from pathlib import Path
sys.path.append(str(Path(__file__).parent.parent / "src"))

from api.hw_spec import HardwareSpec

def test_hw_spec_load():
    try:
        spec = HardwareSpec.load("hw_config")
        print("✅ HardwareSpec 加载成功!")

        # 1. 验证基础参数
        smc_num = spec.params.get("PARAM_SMC_NUM")
        print(f"   [Param] SMC 数量: {smc_num}")
        assert smc_num == 32

        # 2. 验证 FPMUL 的 SIMD 拆分
        fpmul = spec.get_module("FPMUL")
        print(f"   [Module] FPMUL 16-bit 并行度: {fpmul.parallelism['prec_16bit'].lanes} lanes")
        assert fpmul.parallelism['prec_16bit'].lanes == 8

        # 3. 验证 Rtovr 映射矩阵
        dr_name = spec.get_rtovr_dr_name(3) # DR_FPMUL_D
        dvr_name = spec.get_rtovr_dvr_name(5) # DVR_FPADD_S0
        print(f"   [Rtovr] Index 3 消费者: {dr_name}")
        print(f"   [Rtovr] Index 5 生产者: {dvr_name}")
        assert dr_name == "DR_FPMUL_D"
        assert dvr_name == "DVR_FPADD_S0"

        # 4. 验证 UR 微指令位宽
        ur_w = spec.get_module("UR_WRITE")
        print(f"   [UR] 写指令位宽: {ur_w.code_bits} bits")
        assert ur_w.code_bits == 28

        print("\n🎉 Sprint 1 第一阶段验证全部成功!")
    except Exception as e:
        print(f"❌ 验证失败: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    test_hw_spec_load()
