#ifndef INTTOFP_IQ_H
#define INTTOFP_IQ_H

/**
 * @file inttofp_iq.h
 * @brief INTtoFP 模块 IQ：整数转浮点
 * 
 * 对齐 cells/inttofp/vsrc/inttofp.v:
 * i_cru_inttofp[5:0] = {valid, src_is_32b, dst_is_32b, src_signed, src_high, dst_high}
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct IntToFpCru {
    bool para_inst_valid{false};
    bool para_src_is_32b{true};   // 1=INT32, 0=INT16
    bool para_dst_is_32b{true};   // 1=FP32, 0=FP16
    bool para_src_signed{true};   // 1=有符号, 0=无符号
    bool para_src_high{false};    // 1=高位, 0=低位
    bool para_dst_high{false};    // 1=高位, 0=低位
};

class IntToFpIq : public SmiIqBase {
public:
    explicit IntToFpIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint8_t pack_cru_inttofp(const IntToFpCru& para_cru);
    static MicroInstruction make_micro_inst(const IntToFpCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
