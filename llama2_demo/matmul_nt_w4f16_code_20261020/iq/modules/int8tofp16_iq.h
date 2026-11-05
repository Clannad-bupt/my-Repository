#ifndef INT8TOFP16_IQ_H
#define INT8TOFP16_IQ_H

/**
 * @file int8tofp16_iq.h
 * @brief 对齐 Hongjunv0.4.17 `int8_to_fp16_array.v`：cru_int8tofp16[6:0]
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct Int8ToFp16Cru {
    bool para_inst_valid{false};
    bool para_signed_mode{true};
    uint8_t para_smc_id_en{0x1F};
};

class Int8ToFp16Iq : public SmiIqBase {
public:
    explicit Int8ToFp16Iq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint8_t pack_cru_int8tofp16(const Int8ToFp16Cru& para_cru);
    static MicroInstruction make_micro_inst(const Int8ToFp16Cru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
