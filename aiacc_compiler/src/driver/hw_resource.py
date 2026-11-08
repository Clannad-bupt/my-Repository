from enum import Enum, IntEnum

class ResourceType(Enum):
    """
    AIACC 硬件资源物理分类 (逻辑占位符)。
    DFG 在定义时使用这些符号，由 SMT 后端动态映射到物理 IQ ID。
    """
    UR_READ = "UR_READ"
    UR_WRITE = "UR_WRITE"
    RTOVR = "RTOVR"
    FPMUL = "FPMUL"
    FPADD = "FPADD"
    INTADD = "INTADD"
    LOGIC = "LOGIC"
    FPTOFP = "FPTOFP"
    FPTOINT = "FPTOINT"
    INTTOFP = "INTTOFP"
    INTTOINT = "INTTOINT"
    INT8TOFP16 = "INT8TOFP16"
    FP16MUL = "FP16MUL"
    FP32ADDTREE5TO1 = "FP32ADDTREE5TO1"
    FP32ADDTREE8TO1 = "FP32ADDTREE8TO1"
    SHIFTUP = "SHIFTUP"
    SHIFTDN = "SHIFTDN"

class PortSlot(IntEnum):
    """
    RtoVR 网格物理插槽占位符。
    映射到 constants.py 中的物理编号。
    
    🛸 [V4.0 更新]：对齐 INT8TOFP16 和多字并行接口。
    """
    # --- 消费者 (ALU 输入插槽 / RtoVR Destinations) ---
    INTADD_S0 = 0
    INTADD_S1 = 1
    INTADD_S2 = 2
    FPMUL_S0  = 3
    FPMUL_S1  = 4
    FPADD_S0  = 5
    FPADD_S1  = 6
    FPTOFP_S  = 7
    INTTOINT_S = 8
    FPTOINT_S = 9
    INTTOFP_S = 10
    LOGIC_S0  = 11
    LOGIC_S1  = 12
    LOGIC_ST  = 13
    VIN0_S    = 14
    VIN1_S    = 15
    VOUT0_S   = 16
    INT8TOFP16_S0 = 17
    INT8TOFP16_S1 = 18
    FP16MUL_S0 = 19
    FP16MUL_S1 = 20
    FP16MUL_S2 = 21
    FP16MUL_S3 = 22
    FP16MUL_S4567 = 23
    FP32ADDTREE5TO1_S0 = 24
    FP32ADDTREE5TO1_S1 = 25
    FP32ADDTREE5TO1_S2 = 26
    FP32ADDTREE5TO1_S3 = 27
    FP32ADDTREE5TO1_S4 = 28
    FP32ADDTREE5TO1_S5 = 29
    FP32ADDTREE5TO1_S6 = 30
    FP32ADDTREE5TO1_S7 = 31
    FP32ADDTREE8TO1_S0 = 32
    FP32ADDTREE8TO1_S1 = 33
    SHIFTDN_S = 34
    UR_W0_S   = 35
    UR_W1_S   = 36
    UR_W2_S   = 37
    UR_W3_S   = 38
    
    # --- 生产者 (ALU 输出端口 / RtoVR Sources) ---
    INTADD_D0 = 0
    INTADD_D1 = 1
    INTADD_ST = 2
    FPMUL_D   = 3
    FPADD_D   = 4
    FPADD_ST  = 5
    FPTOFP_D  = 6
    INTTOINT_D = 7
    FPTOINT_D = 8
    INTTOFP_D = 9
    LOGIC_D   = 10
    VIN0_D    = 11
    VIN1_D    = 12
    VOUT0_D   = 13
    INT8TOFP16_D0 = 14
    INT8TOFP16_D1 = 15
    INT8TOFP16_D2 = 16
    INT8TOFP16_D3 = 17
    FP16MUL_D0 = 18
    FP16MUL_D1 = 19
    FP16MUL_D2 = 20
    FP16MUL_D3 = 21
    FP16MUL_D4 = 22
    FP16MUL_D5 = 23
    FP16MUL_D6 = 24
    FP16MUL_D7 = 25
    FP32ADDTREE5TO1_D0 = 26
    FP32ADDTREE5TO1_D1 = 27
    FP32ADDTREE8TO1_D = 28
    SHIFTUP_D = 29
    UR_R0_D   = 30
    UR_R1_D   = 31
    UR_R2_D   = 32
    UR_R3_D   = 33
