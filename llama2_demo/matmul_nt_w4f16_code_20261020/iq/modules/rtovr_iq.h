#ifndef RTOVR_IQ_H
#define RTOVR_IQ_H

/**
 * @file rtovr_iq.h
 * @brief RtoVR 模块 IQ：提供符合 280-bit (40组*7bit) 设计的打包接口。
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

/**
 * @brief RtoVR 单个输出选择字段
 * 对应硬件单元: {select[6:1], valid[0]}，共 7 bits
 */
struct RtoVrOutSel {
    bool para_valid{false};   // 映射到 7-bit 单元的 bit 0
    uint8_t para_select{0};  // 6位选择，映射到 7-bit 单元的 bit 6:1
};

/**
 * @brief RtoVR 完整控制字段 (280 bits)
 */
struct RtoVrCru {
    bool para_vld{true};         // 微指令全局有效标志
    RtoVrOutSel para_out[40]{};  // 40 个通道
};

class RtoVrIq : public SmiIqBase {
public:
    explicit RtoVrIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    /**
     * @brief 打包 280-bit 数据到 10 个 32-bit words 中。
     * 数据分布在 out_words[0..8]，out_words[9] 为 0。
     */
    static void pack_cru_rtovr_words(const RtoVrCru& para_cru, uint32_t out_words[10]);

    /**
     * @brief 生成 10 条 MicroInstruction 序列发送给硬件。
     */
    static void make_micro_insts(const RtoVrCru& para_cru, MicroInstruction out_insts[10]);
};

} // namespace iq
} // namespace aiacc

#endif // RTOVR_IQ_H
