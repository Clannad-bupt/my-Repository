#ifndef REMAINDER_SCALAR_OP_H
#define REMAINDER_SCALAR_OP_H

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "../../app/app_json_parser.h"
#include "../../iq/modules/fpadd_iq.h"
#include "../../iq/modules/fpmul_iq.h"
#include "../../iq/modules/fptoint_iq.h"
#include "../../iq/modules/intadd_iq.h"
#include "../../iq/modules/inttofp_iq.h"
#include "../../iq/modules/logical_iq.h"
#include "../../iq/modules/ur_iq.h"
#include "../common/op_base.h"
#include "../common/op_instructions_base.h"
#include "../common/op_registry.h"

namespace aiacc {
namespace op {

enum class RemainderScalarDataType : uint8_t {
    INT32 = 0
};

struct RemainderScalarParams {
    uint32_t para_n{1};
    uint32_t para_c{1};
    uint32_t para_h{1};
    uint32_t para_w{4};
    RemainderScalarDataType para_dtype{RemainderScalarDataType::INT32};

    uint8_t para_input_bank{0};
    uint8_t para_input_addr{0};
    uint8_t para_scalar_bank{1};
    uint8_t para_scalar_addr{0};
    uint8_t para_const_bank{3};
    uint8_t para_x0_addr{0};
    uint8_t para_const2_addr{1};
    uint8_t para_one_addr{2};
    uint8_t para_zero_addr{3};
    uint8_t para_shift_base_addr{4};
    uint8_t para_output_bank{2};
    uint8_t para_output_addr{0};

    int32_t para_scalar_value{7};
    uint32_t para_newton_iters{4};
    uint8_t para_smc_id_en{7};

    uint32_t get_elems_per_word() const { return 4; }
};

struct RemainderScalarInstructions : public OpInstructionsBase {
    struct SchedEntry {
        uint32_t abs_cycle{0};
        uint64_t code{0};
    };

    std::vector<SchedEntry> sched_ur_r0;
    std::vector<SchedEntry> sched_ur_r1;
    std::vector<SchedEntry> sched_ur_r2;
    std::vector<SchedEntry> sched_ur_w0;
    std::vector<SchedEntry> sched_rtovr_0;
    std::vector<SchedEntry> sched_rtovr_1;
    std::vector<SchedEntry> sched_rtovr_3;
    std::vector<SchedEntry> sched_rtovr_4;
    std::vector<SchedEntry> sched_rtovr_5;
    std::vector<SchedEntry> sched_rtovr_6;
    std::vector<SchedEntry> sched_rtovr_9;
    std::vector<SchedEntry> sched_rtovr_10;
    std::vector<SchedEntry> sched_rtovr_11;
    std::vector<SchedEntry> sched_rtovr_12;
    std::vector<SchedEntry> sched_rtovr_13;
    std::vector<SchedEntry> sched_rtovr_35;
    std::vector<SchedEntry> sched_intadd;
    std::vector<SchedEntry> sched_fpadd;
    std::vector<SchedEntry> sched_fpmul;
    std::vector<SchedEntry> sched_fptoint;
    std::vector<SchedEntry> sched_inttofp;
    std::vector<SchedEntry> sched_logic;

    uint32_t para_total_steps{0};
    uint32_t para_total_elements{0};
    uint32_t para_total_cycles{0};
    uint8_t para_smc_id_en{7};
    RemainderScalarDataType para_dtype{RemainderScalarDataType::INT32};

    std::vector<IqInstructionFile> get_iq_files() const override;
    std::string get_operator_name() const override { return "RemainderScalar"; }
    uint32_t get_total_steps() const override { return para_total_steps; }
    uint32_t get_total_elements() const override { return para_total_elements; }
    std::vector<uint8_t> serialize() const override;
};

class RemainderScalarOp : public OpBase {
public:
    std::string get_name() const override;
    OpInstructionsBase* compile(const app::AppOperatorRequest& req) override;
    bool calculate_golden(const app::AppOperatorRequest& req,
                          std::vector<uint8_t>& golden_bin) override;
    bool generate_data(const app::AppOperatorRequest& req,
                       std::vector<uint8_t>& input_bin,
                       std::vector<uint8_t>& other_bin,
                       std::vector<uint8_t>& alpha_bin) override;
    void fill_random_request(app::AppOperatorRequest& req,
                             std::mt19937& rng,
                             std::uniform_real_distribution<float>& data_dist,
                             const std::string& dtype_str) override;

    RemainderScalarInstructions compile(const RemainderScalarParams& params);
};

} // namespace op
} // namespace aiacc

#endif // REMAINDER_SCALAR_OP_H


