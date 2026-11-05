#ifndef FPTOINT_IQ_H
#define FPTOINT_IQ_H

/**
 * @file fptoint_iq.h
 * @brief FPtoINT 模块 IQ：浮点转整数
 * 对齐文档
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct FpToIntCru {
    bool para_inst_valid{false};
    bool para_src_is_32b{true};   // 1=FP32, 0=FP16
    bool para_dst_is_32b{true};   // 1=INT32, 0=INT16
    bool para_src_high{false};    // 1=高位, 0=低位
    bool para_dst_high{false};    // 1=高位, 0=低位
};

class FpToIntIq : public SmiIqBase {
public:
    explicit FpToIntIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint8_t pack_cru_fptoint(const FpToIntCru& para_cru);
    static MicroInstruction make_micro_inst(const FpToIntCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
