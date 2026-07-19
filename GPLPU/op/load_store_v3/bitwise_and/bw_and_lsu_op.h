#ifndef BW_AND_LSU_OP_H
#define BW_AND_LSU_OP_H

#include "../lsu_op_base.h"
#include "../../op/bitwise_and/bitwise_and_op.h"

namespace aiacc {
namespace lsu {

class BwAndLsuOp : public LsuOpBase {
public:
    BwAndLsuOp() = default;
    ~BwAndLsuOp() override = default;

    std::string get_name() const override { return "BitwiseAnd"; }

    std::vector<op::IqInstructionFile> generate_compute_iq_files() const override;

    void generate_golden_for_smc(
        uint8_t smc_id,
        const std::map<std::string, std::vector<uint8_t>>& input_datas,
        std::vector<uint8_t>& golden_out) const override;

    static LsuComputeDesc default_compute_desc();
};

} // namespace lsu
} // namespace aiacc

#endif // BW_AND_LSU_OP_H
