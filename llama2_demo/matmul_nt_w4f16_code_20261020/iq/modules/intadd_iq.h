#ifndef INTADD_IQ_H
#define INTADD_IQ_H

/**
 * @file intadd_iq.h
 * @brief 对齐 Hongjunv0.4.17 `smc/intadd/intadd.v`：cru_intadd[17:0]
 */

#include <cstdint>

#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct IntAddCru {
    bool para_inst_valid{false};
    uint8_t para_precision_s0{0};
    uint8_t para_precision_s1{0};
    uint8_t para_precision_s2{0};
    bool para_sign_s0{false};
    bool para_sign_s1{false};
    bool para_sign_s2{false};
    bool para_sign_d{false};
    bool para_update_st{false};
    bool para_is_sub{false};
    uint8_t para_smc_id_en{0x1F};
};

class IntAddIq : public SmiIqBase {
public:
    explicit IntAddIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint32_t pack_cru_intadd(const IntAddCru& para_cru);
    static MicroInstruction make_micro_inst(const IntAddCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
