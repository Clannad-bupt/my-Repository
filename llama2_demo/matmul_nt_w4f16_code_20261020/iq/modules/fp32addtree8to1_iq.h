#ifndef FP32ADDTREE8TO1_IQ_H
#define FP32ADDTREE8TO1_IQ_H

/**
 * @file fp32addtree8to1_iq.h
 * @brief 对齐 Hongjunv0.4.17 `fp32_adder_tree_8_inputs_pipeline_8to1.v`：i_cru_fp32addtree8to1[7:0]
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

struct Fp32AddTree8To1Cru {
    bool para_inst_valid{false};
    uint8_t para_dst_reg_enc{0};
    uint8_t para_smc_id_en{0x1F};
};

class Fp32AddTree8To1Iq : public SmiIqBase {
public:
    explicit Fp32AddTree8To1Iq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint8_t pack_cru_fp32addtree8to1(const Fp32AddTree8To1Cru& para_cru);
    static MicroInstruction make_micro_inst(const Fp32AddTree8To1Cru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
