#ifndef FP32ADDTREE5TO1_IQ_H
#define FP32ADDTREE5TO1_IQ_H

/**
 * @file fp32addtree5to1_iq.h
 * @brief 对齐 v0.4.19 `fp32_adder_tree_final_pipeline_5to1.v`：cru_fp32addtree5to1[6:0]
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

// v0.4.19 smi_iq.v IQ5: BIT_CODE=2+5=7, WR_INST_WIDTH=41+7=48
constexpr uint16_t kFp32AddTree5To1CodeBits = 7;
constexpr uint16_t kFp32AddTree5To1InstBits = 41 + kFp32AddTree5To1CodeBits;

struct Fp32AddTree5To1Cru {
    bool para_inst_valid{false};
    bool para_accumulate{false};
    uint8_t para_smc_id_en{0x1F};
};

class Fp32AddTree5To1Iq : public SmiIqBase {
public:
    explicit Fp32AddTree5To1Iq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint8_t pack_cru_fp32addtree5to1(const Fp32AddTree5To1Cru& para_cru);
    static MicroInstruction make_micro_inst(const Fp32AddTree5To1Cru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
