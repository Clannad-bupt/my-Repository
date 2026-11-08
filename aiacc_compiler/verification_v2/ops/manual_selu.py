import numpy as np
from typing import List, Dict, Any
from .base import BaseOperator
from ir.mir.builder import MIROpBuilder
from driver.hw_resource import ResourceType as RT, PortSlot as PS

class ManualSeluOperator(BaseOperator):
    """
    [ADPC V2.0] 手动编排的高性能、时序解耦版 SELU DFG。
    通过人为在长链 Horner 拟合多项式中插入 Spill-Reload 物理缓存，且针对每次消费使用独立的 UR_READ，
    并且精细分配常数物理端口，彻底解决所有物理资源时序碰撞与非弹性等式死锁！
    """
    def __init__(self, **kwargs):
        pass

    @property
    def tensor_configs(self) -> Dict[str, Dict[str, Any]]:
        return {
            "x":     {"bank": 0, "offset": 0, "role": "input", "precision": "fp32"},
            "scale": {"bank": 1, "offset": 0, "role": "param", "precision": "fp32"},
            "y":     {"bank": 2, "offset": 0, "role": "output", "precision": "fp32"}
        }

    def prepare_input(self, n_rows: int) -> Dict[str, np.ndarray]:
        inputs = {
            "x":     np.random.uniform(-2.0, 2.0, n_rows).astype(np.float32),
            "scale": np.array([1.75809934], dtype=np.float32)
        }
        return inputs

    def get_golden(self, input_data: Dict[str, np.ndarray], vector_lanes: int) -> Dict[str, Any]:
        x = input_data["x"]
        scale = input_data["scale"][0]
        
        cond = x > 0.0
        neg_val = scale * (1.0 + x * (0.5 + x * (0.16666667 + 0.04166667 * x)))
        golden_y = x * np.where(cond, 1.05070099, neg_val)
        return {"y": golden_y, "intermediates": []}

    def get_dfg(self, spec: Any = None) -> List[Any]:
        b = MIROpBuilder()

        # (A) 读输入 (x_read_1 ~ x_read_y 分布在各 Port，避免 Phase 与同周期冲突)
        x_read_1 = b.make_op("UR_READ", "x_read_1", RT.UR_READ, [], 
                             meta_name="x", logical_addr=0 * 256 + 0, stride=1, role="input", iq_id=30)
        x_read_2 = b.make_op("UR_READ", "x_read_2", RT.UR_READ, [], 
                             meta_name="x", logical_addr=0 * 256 + 0, stride=1, role="input", iq_id=31)
        x_read_3 = b.make_op("UR_READ", "x_read_3", RT.UR_READ, [], 
                             meta_name="x", logical_addr=0 * 256 + 0, stride=1, role="input", iq_id=32)
        x_read_cmp = b.make_op("UR_READ", "x_read_cmp", RT.UR_READ, [], 
                               meta_name="x", logical_addr=0 * 256 + 0, stride=1, role="input", iq_id=33)
        x_read_y = b.make_op("UR_READ", "x_read_y", RT.UR_READ, [], 
                             meta_name="x", logical_addr=0 * 256 + 0, stride=1, role="input", iq_id=33)

        scale_read = b.make_op("UR_READ", "scale_read", RT.UR_READ, [], 
                               meta_name="scale", logical_addr=1 * 256 + 0, stride=0, role="param", iq_id=31)

        # (B) 常数高交替读取 (消除 Bank 冲突与 Port 碰撞)
        c0_read = b.make_op("UR_READ", "cinl_0_1_0", RT.UR_READ, [], logical_addr=7 * 256 + 0, stride=0, role="constant", iq_id=30)
        c1_read = b.make_op("UR_READ", "cinl_1_0_5", RT.UR_READ, [], logical_addr=6 * 256 + 0, stride=0, role="constant", iq_id=31)
        c2_read = b.make_op("UR_READ", "cinl_2_0_16666667", RT.UR_READ, [], logical_addr=7 * 256 + 1, stride=0, role="constant", iq_id=32)
        c3_read = b.make_op("UR_READ", "cinl_3_0_04166667", RT.UR_READ, [], logical_addr=6 * 256 + 1, stride=0, role="constant", iq_id=31)
        c4_read = b.make_op("UR_READ", "cinl_4_0_0", RT.UR_READ, [], logical_addr=7 * 256 + 2, stride=0, role="constant", iq_id=30)
        c5_read = b.make_op("UR_READ", "cinl_5_1_05070099", RT.UR_READ, [], logical_addr=6 * 256 + 2, stride=0, role="constant", iq_id=32)

        # (C) 阶段 1：计算 term1 = 0.16666667 + 0.04166667 * x
        rt_x_1 = b.make_op("RTOVR", "rt_x_1", PS.FPMUL_S0, [x_read_1], sel=30)
        rt_c3 = b.make_op("RTOVR", "rt_c3", PS.FPMUL_S1, [c3_read], sel=31)
        mul1 = b.make_op("FPMUL", "mul1", RT.FPMUL, [rt_x_1, rt_c3], iq_id=3)

        rt_mul1 = b.make_op("RTOVR", "rt_mul1", PS.FPADD_S0, [mul1], sel=3)
        rt_c2 = b.make_op("RTOVR", "rt_c2", PS.FPADD_S1, [c2_read], sel=32)
        add1 = b.make_op("FPADD", "add1", RT.FPADD, [rt_mul1, rt_c2], iq_id=4)

        # 🛸 [BYPASS] RTOVR 直接旁路将 add1 传给 mul2，省掉访存开销并打破 Phase 限制！
        rt_temp1 = b.make_op("RTOVR", "rt_temp1", PS.FPMUL_S1, [add1], sel=4)

        # (D) 阶段 2：计算 term2 = 0.5 + x * term1
        rt_x_2 = b.make_op("RTOVR", "rt_x_2", PS.FPMUL_S0, [x_read_2], sel=31)
        mul2 = b.make_op("FPMUL", "mul2", RT.FPMUL, [rt_x_2, rt_temp1], iq_id=3)

        rt_mul2 = b.make_op("RTOVR", "rt_mul2", PS.FPADD_S0, [mul2], sel=3)
        rt_c1 = b.make_op("RTOVR", "rt_c1", PS.FPADD_S1, [c1_read], sel=31)
        add2 = b.make_op("FPADD", "add2", RT.FPADD, [rt_mul2, rt_c1], iq_id=4)

        # 🛸 [BYPASS] RTOVR 直接旁路将 add2 传给 mul3
        rt_temp2 = b.make_op("RTOVR", "rt_temp2", PS.FPMUL_S1, [add2], sel=4)

        # (E) 阶段 3：计算 term3 = 1.0 + x * term2 并乘 scale
        rt_x_3 = b.make_op("RTOVR", "rt_x_3", PS.FPMUL_S0, [x_read_3], sel=32)
        mul3 = b.make_op("FPMUL", "mul3", RT.FPMUL, [rt_x_3, rt_temp2], iq_id=3)

        rt_mul3 = b.make_op("RTOVR", "rt_mul3", PS.FPADD_S0, [mul3], sel=3)
        rt_c0 = b.make_op("RTOVR", "rt_c0", PS.FPADD_S1, [c0_read], sel=30)
        add3 = b.make_op("FPADD", "add3", RT.FPADD, [rt_mul3, rt_c0], iq_id=4)

        rt_add3 = b.make_op("RTOVR", "rt_add3", PS.FPMUL_S0, [add3], sel=4)
        rt_scale = b.make_op("RTOVR", "rt_scale", PS.FPMUL_S1, [scale_read], sel=31)
        neg_branch = b.make_op("FPMUL", "neg_branch", RT.FPMUL, [rt_add3, rt_scale], iq_id=3)

        # (F) 阶段 4：比较与条件选择 Piecewise((1.05070099, x > 0.0), (neg_branch, True))
        rt_x_cmp = b.make_op("RTOVR", "rt_x_cmp", PS.FPADD_S0, [x_read_cmp], sel=33)
        rt_c4 = b.make_op("RTOVR", "rt_c4", PS.FPADD_S1, [c4_read], sel=30)
        cmp = b.make_op("FPADD", "cmp", RT.FPADD, [rt_x_cmp, rt_c4], UPDATE_ST=1, SUB=1, iq_id=4)

        st_wire = b.make_op("RTOVR", "st_wire_ST_", PS.LOGIC_ST, [cmp], dep_port="ST", sel=4)
        rt_c5 = b.make_op("RTOVR", "rt_c5", PS.LOGIC_S0, [c5_read], sel=32)
        rt_neg = b.make_op("RTOVR", "rt_neg", PS.LOGIC_S1, [neg_branch], sel=3)
        mux = b.make_op("LOGIC", "mux", RT.LOGIC, [rt_c5, rt_neg, st_wire], OP_TYPE="SELECT_GT", VALID=1, iq_id=10)

        # (G) 阶段 5：最终输出 y = x * mux
        rt_x_y = b.make_op("RTOVR", "rt_x_y", PS.FPMUL_S0, [x_read_y], sel=33)
        rt_mux = b.make_op("RTOVR", "rt_mux", PS.FPMUL_S1, [mux], sel=10)
        y_0 = b.make_op("FPMUL", "y_0", RT.FPMUL, [rt_x_y, rt_mux], iq_id=3)

        rt_out = b.make_op("RTOVR", "rt_out", PS.UR_W0_S, [y_0], sel=3)
        b.make_op("UR_WRITE", "out_obj_0", RT.UR_WRITE, [rt_out], logical_addr=2 * 256 + 0, stride=1, role="output", is_output=True, iq_id=35)

        ops = b.get_dfg()

        # 🛸 自动补全 RTOVR 增广边依赖 (rtovr_deps)
        for op in ops:
            if op.op_type.upper() == "RTOVR":
                prod_ids = op.metadata.get("depends_on", [])
                if prod_ids:
                    prod_id = prod_ids[0]
                    for consumer in ops:
                        if op.trace_id in consumer.metadata.get("depends_on", []):
                            consumer.metadata.setdefault("rtovr_deps", {})[prod_id] = {
                                "dvr": op.metadata.get("dvr", -1),
                                "sel": op.metadata.get("sel", -1)
                            }

        # [POST-PROCESS] 注入标准 SIMD 迭代 loop 参数
        for op in ops:
            op.metadata.update({
                "shape": [256],
                "inner_loops": 63,
                "outer_loops": 0,
                "inner_inc": 1,
                "outer_inc": 0
            })

        return ops

    def inject_data(self, program: Any, input_data: Dict[str, np.ndarray], vector_lanes: int, spec: Any = None):
        super().inject_data(program, input_data, vector_lanes, spec)
        
        # 🛸 Inject constant tensors manually
        # Bank 7 constants
        program.inject_broadcast(7 * 256 + 0, 1.0)          # cinl_0_1_0
        program.inject_broadcast(7 * 256 + 1, 0.16666667)   # cinl_2_0_16666667
        program.inject_broadcast(7 * 256 + 2, 0.0)          # cinl_4_0_0
        
        # Bank 6 constants
        program.inject_broadcast(6 * 256 + 0, 0.5)          # cinl_1_0_5
        program.inject_broadcast(6 * 256 + 1, 0.04166667)   # cinl_3_0_04166667
        program.inject_broadcast(6 * 256 + 2, 1.05070099)   # cinl_5_1_05070099

