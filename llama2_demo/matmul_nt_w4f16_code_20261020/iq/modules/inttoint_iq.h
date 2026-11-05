#ifndef INTTOINT_IQ_H
#define INTTOINT_IQ_H

/**
 * @file inttoint_iq.h
 * @brief INTtoINT 模块 IQ：整数类型转换
 * 
 * 对齐 cells/int_to_int/vsrc/int_to_int.v:
 * 控制信号：{instr_vld, src_prec, dst_prec, src_signed, dst_signed, src_pos, dst_pos}
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct IntToIntCru {
    bool para_inst_valid{false};
    bool para_src_is_32b{true};   // 1=INT32, 0=INT16
    bool para_dst_is_32b{true};   // 1=INT32, 0=INT16
    bool para_src_signed{true};   // 1=有符号, 0=无符号
    bool para_dst_signed{true};   // 1=有符号, 0=无符号
    bool para_src_high{false};    // 1=高位, 0=低位
    bool para_dst_high{false};    // 1=高位, 0=低位
};

class IntToIntIq : public SmiIqBase {
public:
    explicit IntToIntIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint8_t pack_cru_inttoint(const IntToIntCru& para_cru);
    static MicroInstruction make_micro_inst(const IntToIntCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
