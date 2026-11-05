#ifndef IQ_ID_DEFS_H
#define IQ_ID_DEFS_H

/**
 * @file iq_id_defs.h
 * @brief SMI_IQ 指令队列 ID 统一定义
 * 
 * 本文件集中管理所有 IQ ID 的分配，确保：
 * 1. ID 分配的一致性
 * 2. 避免 ID 冲突
 * 3. 便于维护和扩展
 */

#include <cstdint>
#include <string>

namespace aiacc {
namespace iq {

// ==========================================
// IQ ID 定义
// ==========================================

// UR (User Register) 相关 - IQ 16-23（v4.17 RTL 对齐）
constexpr uint32_t IQ_ID_UR_RDATA_0 = 16;
constexpr uint32_t IQ_ID_UR_RDATA_1 = 17;
constexpr uint32_t IQ_ID_UR_RDATA_2 = 18;
constexpr uint32_t IQ_ID_UR_RDATA_3 = 19;
constexpr uint32_t IQ_ID_UR_WDATA_0 = 20;
constexpr uint32_t IQ_ID_UR_WDATA_1 = 21;
constexpr uint32_t IQ_ID_UR_WDATA_2 = 22;
constexpr uint32_t IQ_ID_UR_WDATA_3 = 23;


// RtoVR (Router to VR) - IQ 28-91（v4.17 RTL 对齐）
constexpr uint32_t IQ_ID_RTOVR_BASE = 28;
constexpr uint32_t IQ_ID_RTOVR_END = 91;

// 常用路由 IQ（使用 RtoVR 范围内的 ID）
constexpr uint32_t IQ_ID_RTOVR_TO_FPMUL = 28;  // AddOp: 路由到 FPMUL
constexpr uint32_t IQ_ID_RTOVR_TO_FPADD = 29;  // AddOp: 路由到 FPADD
constexpr uint32_t IQ_ID_RTOVR_TO_UR = 30;     // 通用: 路由回 UR
constexpr uint32_t IQ_ID_RTOVR_INIT = 31;      // PowOp: 初始化路由
constexpr uint32_t IQ_ID_RTOVR_ACCUM = 32;     // PowOp: 累加路由

// VIN/VOUT 使能 - IQ 68-70（v4.17 RTL 固定映射：
// smi_top.v 写死 iq_inst_vld[68..70] -> VIN0/VIN1/VOUT0 CRU）
// 注意: IQ67 是 RtoVR 备用 lane39（reserved_out_39，RTL 内 tie-0 无消费者）；
//       IQ71-91 保留未实现。这些号均不可使用，写指令等于白写。
constexpr uint32_t IQ_ID_VIN0  = 68;  // VIN0 (RTL 固定)
constexpr uint32_t IQ_ID_VIN1  = 69;  // VIN1 (RTL 固定)
constexpr uint32_t IQ_ID_VOUT0 = 70;  // VOUT0 (RTL 固定)

// 计算单元（Compute）- v4.17 RTL 对齐
constexpr uint32_t IQ_ID_INTADD            = 0;   // INTADD
constexpr uint32_t IQ_ID_INT8_TO_FP16      = 1;   // INT8toFP16
constexpr uint32_t IQ_ID_FP16MUL           = 2;   // FP16MUL
constexpr uint32_t IQ_ID_FP16MUL_S0123     = 3;   // FP16MUL-S0123
constexpr uint32_t IQ_ID_FP16MUL_S4567     = 4;   // FP16MUL-S4567
constexpr uint32_t IQ_ID_FP32_ADDTREE_5TO1 = 5;   // FP32ADDTREE5TO1
constexpr uint32_t IQ_ID_FP32_ADDTREE_8TO1 = 6;   // FP32ADDTREE8TO1
constexpr uint32_t IQ_ID_FPMUL             = 7;   // FPMUL
constexpr uint32_t IQ_ID_FPADD             = 8;   // FPADD
constexpr uint32_t IQ_ID_FP_TO_FP          = 9;   // FPtoFP
constexpr uint32_t IQ_ID_INT_TO_INT        = 10;  // INTtoINT
constexpr uint32_t IQ_ID_FP_TO_INT         = 11;  // FPtoINT (v4.17: shares with FPtoFP at 9/11)
constexpr uint32_t IQ_ID_INT_TO_FP         = 12;  // INTtoFP (v4.17: shares with LOGIC at 12/13)
constexpr uint32_t IQ_ID_LOGIC             = 13;  // LOGIC
constexpr uint32_t IQ_ID_SHIFTUP           = 14;  // SHIFTUP
constexpr uint32_t IQ_ID_SHIFTDN           = 15;  // SHIFTDN
constexpr uint32_t IQ_ID_STB               = 24;  // STB (UR-read side)
constexpr uint32_t IQ_ID_LDB               = 26;  // LDB (UR-write side)

// 数据搬运相关（AXI Stream 连接侧与 Data Shuffle）
// v4.17: STB-AXI=25, LDB-AXI=27, DS=92
constexpr uint32_t IQ_ID_STBAXI            = 25;  // STBAXI (AXI-write side)
constexpr uint32_t IQ_ID_LDBAXI            = 27;  // LDBAXI (AXI-read side)
constexpr uint32_t IQ_ID_DS                = 92;  // DS (Data Shuffle)
constexpr uint32_t IQ_ID_SMI_DS_T          = 93;  // SMI_DS_T (transpose)

// 预留扩展
constexpr uint32_t IQ_ID_RESERVED_START = 94;
constexpr uint32_t IQ_ID_RESERVED_END = 255;

// ==========================================
// IQ 名称定义
// ==========================================

// UR 相关
constexpr const char* IQ_NAME_UR_RDATA_0 = "ur_rdata_0";
constexpr const char* IQ_NAME_UR_RDATA_1 = "ur_rdata_1";
constexpr const char* IQ_NAME_UR_RDATA_2 = "ur_rdata_2";
constexpr const char* IQ_NAME_UR_RDATA_3 = "ur_rdata_3";
constexpr const char* IQ_NAME_UR_WDATA_0 = "ur_wdata_0";
constexpr const char* IQ_NAME_UR_WDATA_1 = "ur_wdata_1";
constexpr const char* IQ_NAME_UR_WDATA_2 = "ur_wdata_2";
constexpr const char* IQ_NAME_UR_WDATA_3 = "ur_wdata_3";


// RtoVR 相关
constexpr const char* IQ_NAME_RTOVR_TO_FPMUL = "rtovr_to_fpmul";
constexpr const char* IQ_NAME_RTOVR_TO_FPADD = "rtovr_to_fpadd";
constexpr const char* IQ_NAME_RTOVR_TO_UR = "rtovr_to_ur";
constexpr const char* IQ_NAME_RTOVR_INIT = "rtovr_init";
constexpr const char* IQ_NAME_RTOVR_ACCUM = "rtovr_accum";

// VIN/VOUT
constexpr const char* IQ_NAME_VIN0 = "vin0";
constexpr const char* IQ_NAME_VIN1 = "vin1";
constexpr const char* IQ_NAME_VOUT0 = "vout0";

// 计算单元
constexpr const char* IQ_NAME_INTADD = "intadd";
constexpr const char* IQ_NAME_INT8_TO_FP16 = "int8tofp16";
constexpr const char* IQ_NAME_FP16MUL = "fp16mul";
constexpr const char* IQ_NAME_FP16MUL_S0123 = "fp16mul_s0123";
constexpr const char* IQ_NAME_FP16MUL_S4567 = "fp16mul_s4567";
constexpr const char* IQ_NAME_FP32_ADDTREE_5TO1 = "fp32addtree5to1";
constexpr const char* IQ_NAME_FP32_ADDTREE_8TO1 = "fp32addtree8to1";
constexpr const char* IQ_NAME_FPMUL = "fpmul";
constexpr const char* IQ_NAME_FPADD = "fpadd";
constexpr const char* IQ_NAME_FP_TO_FP = "fptofp";
constexpr const char* IQ_NAME_INT_TO_INT = "inttoint";
constexpr const char* IQ_NAME_FP_TO_INT = "fptoint";
constexpr const char* IQ_NAME_INT_TO_FP = "inttofp";
constexpr const char* IQ_NAME_LOGIC = "logic";
constexpr const char* IQ_NAME_SHIFTUP = "shiftup";
constexpr const char* IQ_NAME_SHIFTDN = "shiftdn";
constexpr const char* IQ_NAME_STB = "stb";
constexpr const char* IQ_NAME_LDB = "ldb";
constexpr const char* IQ_NAME_STBAXI = "stbaxi";
constexpr const char* IQ_NAME_LDBAXI = "ldbaxi";
constexpr const char* IQ_NAME_DS = "ds";
constexpr const char* IQ_NAME_SMI_DS_T = "smi_ds_t";

// ==========================================
// 辅助函数
// ==========================================

/**
 * @brief 根据 IQ ID 获取默认名称
 * @param iq_id IQ 编号
 * @return IQ 名称字符串
 */
inline std::string get_iq_name(uint32_t iq_id) {
    switch (iq_id) {
        // UR
        case IQ_ID_UR_RDATA_0: return IQ_NAME_UR_RDATA_0;
        case IQ_ID_UR_RDATA_1: return IQ_NAME_UR_RDATA_1;
        case IQ_ID_UR_RDATA_2: return IQ_NAME_UR_RDATA_2;
        case IQ_ID_UR_RDATA_3: return IQ_NAME_UR_RDATA_3;
        case IQ_ID_UR_WDATA_0: return IQ_NAME_UR_WDATA_0;
        case IQ_ID_UR_WDATA_1: return IQ_NAME_UR_WDATA_1;
        case IQ_ID_UR_WDATA_2: return IQ_NAME_UR_WDATA_2;
        case IQ_ID_UR_WDATA_3: return IQ_NAME_UR_WDATA_3;
        
        // RtoVR
        case IQ_ID_RTOVR_TO_FPMUL: return IQ_NAME_RTOVR_TO_FPMUL;
        case IQ_ID_RTOVR_TO_FPADD: return IQ_NAME_RTOVR_TO_FPADD;
        case IQ_ID_RTOVR_TO_UR: return IQ_NAME_RTOVR_TO_UR;
        case IQ_ID_RTOVR_INIT: return IQ_NAME_RTOVR_INIT;
        case IQ_ID_RTOVR_ACCUM: return IQ_NAME_RTOVR_ACCUM;

        // VIN/VOUT
        case IQ_ID_VIN0: return IQ_NAME_VIN0;
        case IQ_ID_VIN1: return IQ_NAME_VIN1;
        case IQ_ID_VOUT0: return IQ_NAME_VOUT0;
        
        // 计算单元
        case IQ_ID_INTADD: return IQ_NAME_INTADD;
        case IQ_ID_INT8_TO_FP16: return IQ_NAME_INT8_TO_FP16;
        case IQ_ID_FP16MUL: return IQ_NAME_FP16MUL;
        case IQ_ID_FP16MUL_S0123: return IQ_NAME_FP16MUL_S0123;
        case IQ_ID_FP16MUL_S4567: return IQ_NAME_FP16MUL_S4567;
        case IQ_ID_FP32_ADDTREE_5TO1: return IQ_NAME_FP32_ADDTREE_5TO1;
        case IQ_ID_FP32_ADDTREE_8TO1: return IQ_NAME_FP32_ADDTREE_8TO1;
        case IQ_ID_FPMUL: return IQ_NAME_FPMUL;
        case IQ_ID_FPADD: return IQ_NAME_FPADD;
        case IQ_ID_FP_TO_FP: return IQ_NAME_FP_TO_FP;
        case IQ_ID_INT_TO_INT: return IQ_NAME_INT_TO_INT;
        case IQ_ID_FP_TO_INT: return IQ_NAME_FP_TO_INT;
        case IQ_ID_INT_TO_FP: return IQ_NAME_INT_TO_FP;
        case IQ_ID_LOGIC: return IQ_NAME_LOGIC;
        case IQ_ID_SHIFTUP: return IQ_NAME_SHIFTUP;
        case IQ_ID_SHIFTDN: return IQ_NAME_SHIFTDN;
        case IQ_ID_STB: return IQ_NAME_STB;
        case IQ_ID_LDB: return IQ_NAME_LDB;
        case IQ_ID_STBAXI: return IQ_NAME_STBAXI;
        case IQ_ID_LDBAXI: return IQ_NAME_LDBAXI;
        case IQ_ID_DS: return IQ_NAME_DS;
        case IQ_ID_SMI_DS_T: return IQ_NAME_SMI_DS_T;

        default:
            return "unknown_iq_" + std::to_string(iq_id);
    }
}

/**
 * @brief 检查 IQ ID 是否有效
 * @param iq_id IQ 编号
 * @return true 如果 ID 在有效范围内
 */
inline bool is_valid_iq_id(uint32_t iq_id) {
    return iq_id <= IQ_ID_RESERVED_END;
}

/**
 * @brief 检查 IQ ID 是否为 UR 相关
 */
inline bool is_ur_iq(uint32_t iq_id) {
    return iq_id >= IQ_ID_UR_RDATA_0 && iq_id <= IQ_ID_UR_WDATA_3;
}

/**
 * @brief 检查 IQ ID 是否为 RtoVR 相关
 */
inline bool is_rtovr_iq(uint32_t iq_id) {
    return iq_id >= IQ_ID_RTOVR_BASE && iq_id <= IQ_ID_RTOVR_END;
}

/**
 * @brief 检查 IQ ID 是否为计算单元
 */
inline bool is_compute_iq(uint32_t iq_id) {
    return iq_id >= IQ_ID_INTADD && iq_id <= IQ_ID_LDB;
}

/**
 * @brief 检查 IQ ID 是否为数据搬运/整理相关
 * （STB, LDB, STBAXI, LDBAXI, DS）
 */
inline bool is_data_movement_iq(uint32_t iq_id) {
    return iq_id == IQ_ID_STB || iq_id == IQ_ID_LDB ||
           iq_id == IQ_ID_STBAXI || iq_id == IQ_ID_LDBAXI ||
           iq_id == IQ_ID_DS;
}

} // namespace iq
} // namespace aiacc

#endif // IQ_ID_DEFS_H
