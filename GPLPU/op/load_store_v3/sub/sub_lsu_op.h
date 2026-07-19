#ifndef SUB_LSU_OP_V3_H
#define SUB_LSU_OP_V3_H

#include "../lsu_op_base.h"
#include "../../op/sub/sub_op.h"

namespace aiacc {
namespace lsu {

class SubLsuOp : public LsuOpBase {
public:
    SubLsuOp() = default;
    ~SubLsuOp() override = default;

    std::string get_name() const override { return "sub"; }

    std::vector<op::IqInstructionFile> generate_compute_iq_files() const override;

    void generate_hbm_data(uint8_t smc_id, uint32_t seed,
                           std::vector<uint8_t>& hbm_bytes) const override;

    void generate_golden_for_smc(
        uint8_t smc_id,
        const std::map<std::string, std::vector<uint8_t>>& input_datas,
        std::vector<uint8_t>& golden_out) const override;

    static LsuComputeDesc default_compute_desc();
};

} // namespace lsu
} // namespace aiacc

#endif // SUB_LSU_OP_V3_H
