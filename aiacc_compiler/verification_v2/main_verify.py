import sys, os, argparse, json
import numpy as np

# 调整路径以支持导入 src 和 verification_v2
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'src'))

from verification_v2.infra.mock_memory import MockProgram
from verification_v2.infra.visualizer import print_physical_instructions, print_aligned_timeline
from verification_v2.infra.pipeline_runner import PipelineRunner
from verification_v2.infra.auditor import Auditor
from verification_v2.ops.horner import HornerOperator
from verification_v2.ops.taylor import TaylorOperator
from verification_v2.ops.configurable import ConfigurableOperator
from verification_v2.ops.matmul_op import ManualMatmulOperator
from verification_v2.ops.manual_selu import ManualSeluOperator

OPERATOR_MAP = {
    "horner": HornerOperator,
    "sin3":   HornerOperator,
    "sin5":   HornerOperator,
    "sin5_taylor": TaylorOperator,

    "manual_selu": ManualSeluOperator,
    "selu": ManualSeluOperator,
    # matmul 使用手动 DFG（ManualMatmulOperator），不走 ConfigurableOperator 的
    # compile_with_adpc 路径（后者对 4-operand 公式生成的 DFG 结构 SMT 不可解）
    "matmul": ManualMatmulOperator,
}

CATEGORIES = {
    "math": ["add", "mul", "div", "reciprocal", "sqrt", "rsqrt", "abs", "addcdiv", "addcmul"],
    "trig": ["horner", "sin3", "sin5", "sin5_taylor", "horner_v3", "cos4", "cosh4", "exp5", "expm1", "sinh5", "log", "log1p", "log2", "tan7", "tanh7"],
    "activation": ["elu", "mish", "hardsigmoid", "hardswish", "gelu_approx", "silu_approx", "silu_taylor", "sigmoid_taylor", "leaky_relu", "clip", "relu6", "relu_offset", "selu"],
    "manual": ["manual_selu", "matmul"]
}


# 🛸 [V5.0] 自动扫描 YAML 算子配置并注册
OPS_DIR = os.path.join(os.path.dirname(__file__), "ops")
if os.path.exists(OPS_DIR):
    for f in os.listdir(OPS_DIR):
        if f.endswith(".yaml"):
            op_name = f.replace(".yaml", "")
            if op_name in OPERATOR_MAP:
                continue
            yaml_path = os.path.join(OPS_DIR, f)
            # 使用 lambda 延迟加载，模拟类构造行为
            OPERATOR_MAP[op_name] = lambda path=yaml_path, **kwargs: ConfigurableOperator(path)

# 默认批处理算子参数
DEFAULT_OP_ARGS = {
    "horner": {"coeffs": [0.1, -0.2, 0.3, -0.4, 0.5, -0.6]},
    "sin3":   {"coeffs": [-1/6.0, 0.0, 1.0, 0.0]},
    "sin5":   {"coeffs": [1/120.0, 0.0, -1/6.0, 0.0, 1.0, 0.0]},
    "sin5_taylor": {"coeffs": {5: 1/120.0, 3: -1/6.0, 1: 1.0}}
}

def run_test(op_name: str, n_rows: int, op_args: dict, hw_config_name: str, show_r: bool, show_m: bool, input_file: str = None) -> bool:
    if show_r:
        print("\n" + "="*20 + f" AIACC V2.0 Modular Verification: {op_name.upper()} " + "="*20)
    
    if not show_m:
        import logging
        logging.getLogger("ParallelKernel").setLevel(logging.WARNING)
        logging.getLogger("UCodeSynthesizer").setLevel(logging.WARNING)

    # 1. 动态准备算子与数据
    if op_name not in OPERATOR_MAP:
        print(f"[ERROR] Operator '{op_name}' not found in registry.")
        return False

    # 实例化算子（通过参数注入实现去硬编码）
    operator = OPERATOR_MAP[op_name](**op_args)

    # 加载自定义输入数据或动态生成
    if input_file:
        if not os.path.exists(input_file):
            print(f"[ERROR] Input file '{input_file}' not found.")
            return False
        loaded = np.load(input_file)
        input_data = {k: loaded[k] for k in loaded.files}
        # 动态重设实际行数
        first_key = next(iter(input_data))
        n_rows = len(input_data[first_key])
        if show_r:
            print(f"[INFO] Loaded custom inputs from '{input_file}' ({n_rows} rows).")
    else:
        # 动态生成输入数据（由算子自行决定结构）
        input_data = operator.prepare_input(n_rows)
    
    # 2. 运行流水线
    runner = PipelineRunner(hw_config_name=hw_config_name, show_review=show_r, show_model=show_m)
    ctx = runner.run_full_stack(operator, input_data, op_name=op_name)
    
    if not ctx:
        return False

    # 3. 可视化调试
    print_physical_instructions(ctx["physical_instrs"], show_model=show_m)
    
    # 解析实际轨迹用于对齐图 (下沉到 PipelineRunner)
    actual_activity = runner.parse_simulation_timeline(ctx["output_dir"])

    # print_aligned_timeline(ctx["schedule"], actual_activity, ctx["II"], max_cycles=50, show_review=show_m)

    # 4. 分阶段审计 (Fail-Fast)
    auditor = Auditor(ctx["output_dir"], runner.spec, show_review=show_r, show_model=show_m)
    
    if not auditor.audit_physical_headers():
        print("\n[FAIL-FAST] Header Audit failed. Stopping pipeline.")
        return False
        
    sim_ok = ctx.get("sim_success", True)
    align_ok = auditor.audit_truth_alignment(
        ctx["log_path"], 
        num_iterations=ctx["num_iterations"],
        ops=ctx.get("ops"),
        input_data=input_data
    )
    
    if align_ok and sim_ok:
        print("\n" + "*"*10 + f" ALL PHYSICAL CHECKS PASSED FOR {op_name.upper()} " + "*"*10)
        return True
    else:
        print("\n" + "!"*10 + f" VERIFICATION FAILED FOR {op_name.upper()} " + "!"*10)
        # 🚀 [SW-OPT-2] 失败时自动升级 Verbose，无需重跑
        print("\n[AUTO-DEBUG] Replaying physical instructions and timeline for diagnosis:")
        print_physical_instructions(ctx["physical_instrs"], show_model=True)
        actual_activity_debug = runner.parse_simulation_timeline(ctx["output_dir"])
        # print_aligned_timeline(ctx["schedule"], actual_activity_debug, ctx["II"],
        #                        max_cycles=80, show_review=True)
        return False

def run_smt_unit_tests() -> bool:
    """[SW-OPT: 测试集成] 运行 SMT 约束单元测试（不启动仿真器，极快）"""
    import unittest
    print("\n" + "="*20 + " SMT CONSTRAINT UNIT TESTS " + "="*20)
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    try:
        from tests.unit.test_smt_adversarial import TestSMTAdversarial, TestSMTRefutation
        suite.addTests(loader.loadTestsFromTestCase(TestSMTAdversarial))
        suite.addTests(loader.loadTestsFromTestCase(TestSMTRefutation))
    except ImportError as e:
        print(f"[WARN] Could not load SMT unit tests: {e}")
        return True  # 非致命
    runner = unittest.TextTestRunner(verbosity=1)
    result = runner.run(suite)
    return result.wasSuccessful()


def run_hw_parity_tests() -> bool:
    """[SW-OPT: 测试集成] 运行硬件物理比对测试（SMT → .hex → 仿真 → Golden 比对）"""
    import unittest
    print("\n" + "="*20 + " HARDWARE PARITY TESTS " + "="*20)
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    try:
        from tests.integration.test_smt_hw_parity import TestSMTHardwareParity
        suite.addTests(loader.loadTestsFromTestCase(TestSMTHardwareParity))
    except ImportError as e:
        print(f"[WARN] Could not load HW parity tests: {e}")
        return True  # 非致命
    runner = unittest.TextTestRunner(verbosity=1)
    result = runner.run(suite)
    return result.wasSuccessful()


def main():
    parser = argparse.ArgumentParser(description="AIACC V2.0 Modular Verification Suite")
    parser.add_argument("--op", type=str, default="horner",
                        help="Operator name to verify, 'all' for all, or category name ('math', 'trig', 'activation', 'manual')")
    parser.add_argument("--op-args", type=str, default="{}",
                        help="JSON string for operator kwargs")
    parser.add_argument("--hw-config", type=str, default="hw_config",
                        help="Hardware configuration name")
    parser.add_argument("--n-rows", type=int, default=256,
                        help="Number of data rows to verify (Default: 256)")
    parser.add_argument("--input-file", type=str, default=None,
                        help="Path to an optional .npz file containing custom input arrays")
    parser.add_argument("--review", action="store_true", default=True)
    parser.add_argument("--no-review", action="store_false", dest="review")
    parser.add_argument("--model", action="store_true", default=False)
    parser.add_argument("--quiet", action="store_true")
    # 🚀 [SW-OPT: 测试集成] 新增 --mode 参数
    parser.add_argument("--mode", type=str, default="e2e",
                        choices=["e2e", "smt-unit", "hw-parity", "all"],
                        help=(
                             "Verification mode:\n"
                             "  e2e       - End-to-end operator verification (default)\n"
                             "  smt-unit  - SMT constraint unit tests (no simulation)\n"
                             "  hw-parity - Hardware physical parity tests\n"
                             "  all       - All modes in sequence (fail-fast)"
                        ))

    args = parser.parse_args()
    show_r = args.review and (not args.quiet)
    show_m = args.model and (not args.quiet)
    op_args_dict = json.loads(args.op_args)

    # ── 模式路由 ──────────────────────────────────────────────────────────
    if args.mode == "smt-unit":
        sys.exit(0 if run_smt_unit_tests() else 1)

    elif args.mode == "hw-parity":
        sys.exit(0 if run_hw_parity_tests() else 1)

    elif args.mode == "all":
        print("\n" + "="*20 + " FULL VERIFICATION SUITE " + "="*20)
        results = {}
        results["smt-unit"] = run_smt_unit_tests()
        if not results["smt-unit"]:
            print("[FAIL-FAST] SMT unit tests failed. Stopping.")
            sys.exit(1)
        results["hw-parity"] = run_hw_parity_tests()
        if not results["hw-parity"]:
            print("[FAIL-FAST] HW parity tests failed. Stopping.")
            sys.exit(1)
        # 最后跑 e2e
        kwargs = op_args_dict if op_args_dict else DEFAULT_OP_ARGS.get(args.op, {})
        results["e2e"] = run_test(args.op, args.n_rows, kwargs, args.hw_config, show_r, show_m, args.input_file)
        all_ok = all(results.values())
        print("\n" + "="*20 + " FULL SUITE REPORT " + "="*20)
        for k, v in results.items():
            print(f"  [{k.upper():12s}]: {'PASS' if v else 'FAIL'}")
        sys.exit(0 if all_ok else 1)

    else:  # e2e (default)
        if args.op == "all" or args.op in CATEGORIES:
            target_ops = list(OPERATOR_MAP.keys()) if args.op == "all" else CATEGORIES[args.op]
            # Filter out operators not loaded in OPERATOR_MAP (to prevent key errors)
            target_ops = [op for op in target_ops if op in OPERATOR_MAP]
            
            print(f"\n" + "="*20 + f" STARTING BATCH VERIFICATION ({args.op.upper()}) " + "="*20)
            results = {}
            for op in target_ops:
                kwargs = op_args_dict if op_args_dict else DEFAULT_OP_ARGS.get(op, {})
                success = run_test(op, args.n_rows, kwargs, args.hw_config, show_r, show_m, args.input_file)
                results[op] = success

            print("\n" + "="*20 + f" BATCH VERIFICATION REPORT ({args.op.upper()}) " + "="*20)
            all_passed = True
            for op, success in results.items():
                status = "[PASS]" if success else "[FAIL]"
                print(f"[{op.upper()}]: {status}")
                if not success: all_passed = False
            sys.exit(0 if all_passed else 1)
        else:
            kwargs = op_args_dict if op_args_dict else DEFAULT_OP_ARGS.get(args.op, {})
            success = run_test(args.op, args.n_rows, kwargs, args.hw_config, show_r, show_m, args.input_file)
            sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
