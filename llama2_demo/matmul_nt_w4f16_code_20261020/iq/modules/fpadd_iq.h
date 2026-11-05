#ifndef FPADD_IQ_H
#define FPADD_IQ_H

/**
 * @file fpadd_iq.h
 * @brief FPADD 模块 IQ：定义 5-bit 控制字段 FpAddCru。
 * 硬件布局 (10 bits，对齐 fp_adder_rtl.v)：
 * Bit 0: inst_valid
 * Bit 1: src_precision (0:16b, 1:32b)
 * Bit 2: dst_precision (0:16b, 1:32b)
 * Bit 3: update_status
 * Bit 4: is_sub (0:Add, 1:Sub)
 * Bit 9-5: smc_id_en
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct FpAddCru {
    // --- 构造函数：确保代理对象绑定到当前实例 ---
    FpAddCru() : para_mode_is_32b(this), para_update_st(this) {}

    // --- 核心硬件字段 (与最新 spec 对齐) ---
    bool para_inst_valid{false};        // Bit 0
    bool para_src_precision_32b{true};  // Bit 1
    bool para_dst_precision_32b{true};  // Bit 2
    bool para_update_status{false};     // Bit 3
    bool para_is_sub{false};            // Bit 4
    uint8_t para_smc_id_en{0x1F};      // Bit 9-5: smc_id_en (5 bits)

    // --- 兼容性代理 (解决算子层编译报错) ---
    
    // 代理旧字段 para_mode_is_32b
    struct ModeProxy {
        FpAddCru* parent;
        ModeProxy(FpAddCru* p) : parent(p) {}
        void operator=(bool val) {
            parent->para_src_precision_32b = val;
            parent->para_dst_precision_32b = val;
        }
        operator bool() const { return parent->para_src_precision_32b; }
    };

    // 代理旧字段 para_update_st
    struct StatusProxy {
        FpAddCru* parent;
        StatusProxy(FpAddCru* p) : parent(p) {}
        void operator=(bool val) { parent->para_update_status = val; }
        operator bool() const { return parent->para_update_status; }
    };

    // 实例化代理对象
    ModeProxy para_mode_is_32b;
    StatusProxy para_update_st;
};

class FpAddIq : public SmiIqBase {
public:
    explicit FpAddIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    // 打包为 10-bit 格式（对齐 fp_adder_rtl.v）
    static uint16_t pack_cru_fpadd(const FpAddCru& para_cru);

    static MicroInstruction make_micro_inst(const FpAddCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
