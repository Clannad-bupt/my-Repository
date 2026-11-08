"""
AIACC 硬件物理层常量定义 (Hardware Abstraction Layer Macro Definitions)
定义所有与物理架构强相关的端口偏移、指令偏移及位域语义。
"""

# --- 1. 物理指令队列 (Instruction Queue) 基础偏移 ---
IQ_BASE_UR_READ  = 200  # UR-Read 单元在 SMT 中的基础索引
IQ_BASE_UR_WRITE = 204  # UR-Write 单元在 SMT 中的基础索引

# --- 2. 物理数据交换网络 (RtoVR Crossbar) 端口偏移 ---
# 对应 rtovr_config.yaml 中的物理编号
PORT_OFFSET_UR_RDATA = 30  # UR 读取数据进入 Crossbar 的起始编号 (30-33)
PORT_ID_INTADD_D0    = 0   # 整数加法输出端子
PORT_ID_FPMUL_D      = 3   # 浮点乘法输出端子 (Confirmed: Line 8 in yaml is index 3)
PORT_ID_FPADD_D      = 4   # 浮点加法输出端子 (Confirmed: Line 9 in yaml is index 4)

# --- 3. 指令头 (IQ Header) 物理布局定义 ---
# 对应 isa_v5.yaml 及其解码器布局
BITS_HEADER_TOTAL    = 41  # 标准 41-bit IQ Header
OFFSET_VLD           = 40  # VALID 位在 Header 内部的偏移
OFFSET_JUMP          = 39  # JUMP 位在 Header 内部的偏移
OFFSET_LOOPS         = 20  # LOOPS 位在 Header 内部的起始偏移
LENGTH_LOOPS         = 10  # LOOPS 字段位宽

# --- 4. 默认数值与宽放 ---
DEFAULT_BITS_CODE    = 16  # 全局默认 Payload 位宽 (回退用)
DEFAULT_MAX_CYCLES   = 120 # 仿真默认最大周期

# --- 5. 位域语义别名 (Semantic Aliases) [Authoritative Contract Aligned]
SEMANTIC_RE    = ["re", "RE", "vld", "VLD", "VALID"]
SEMANTIC_WE    = ["we", "WE"]
SEMANTIC_BANK  = ["bank_id", "bank", "p_bank_id", "BANK", "rbank", "wbank"]
SEMANTIC_ADDR  = ["p_addr", "address", "ADDR", "raddr", "waddr", "addr"]
SEMANTIC_PREC  = ["src_prec", "dst_prec", "precision", "PREC", "SRC_PREC", "DST_PREC"]
SEMANTIC_SRCID = ["p_src_id", "slot_id", "dr_index", "src_id", "SRC_ID", "SEL"]
SEMANTIC_OP    = ["SUB", "add_sub", "op_mode"]
