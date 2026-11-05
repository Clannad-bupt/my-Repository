#ifndef SMI_DS_T_IQ_H
#define SMI_DS_T_IQ_H

/**
 * @file smi_ds_t_iq.h
 * @brief 对齐 v0.4.19 IQ93 BIT_CODE=5；`smi_top.v` 取低 5-bit 驱动 `smi_ds_t.v` 的 cri_ds_t[4:0]
 *
 * 字段（与当前 RTL 一致）：
 *   [0]   vld
 *   [1]   intlv_en（当前 RTL 未使用，预留位）
 *   [2]   intlv_grp
 *   [3]   intlv_in
 *   [4]   intlv_out
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

constexpr uint32_t kSmiDsTCodeBits = 5;

struct SmiDsTCru {
    bool para_vld{false};
    bool para_intlv_en{false};
    bool para_intlv_grp{false};
    bool para_intlv_in{false};
    bool para_intlv_out{false};
};

class SmiDsTIq : public SmiIqBase {
public:
    explicit SmiDsTIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint64_t pack_cru_smi_ds_t(const SmiDsTCru& para_cru);
    static MicroInstruction make_micro_inst(const SmiDsTCru& para_cru);
};

}  // namespace iq
}  // namespace aiacc

#endif
