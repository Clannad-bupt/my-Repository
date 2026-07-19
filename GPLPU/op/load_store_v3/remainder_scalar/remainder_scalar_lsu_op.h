#ifndef REMAINDER_SCALAR_LSU_OP_H
#define REMAINDER_SCALAR_LSU_OP_H

#include "../lsu_op_base.h"
#include "../../op/remainder_scalar/remainder_scalar_op.h"

namespace aiacc {
namespace lsu {

class RemainderScalarLsuOp : public LsuOpBase {
public:
    std::string get_name() const override { return "remainder_scalar"; }

    std::vector<op::IqInstructionFile> generate_compute_iq_files() const override;
    void generate_golden_for_smc(
        uint8_t smc_id,
        const std::map<std::string, std::vector<uint8_t>>& input_datas,
        std::vector<uint8_t>& golden_out) const override;
    void generate_hbm_data(uint8_t smc_id, uint32_t seed,
                           std::vector<uint8_t>& hbm_bytes) const override;

    static LsuComputeDesc default_compute_desc();
};

} // namespace lsu
} // namespace aiacc

#endif // REMAINDER_SCALAR_LSU_OP_H
