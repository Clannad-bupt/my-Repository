#ifndef DIV_SCALAR_OP_H
#define DIV_SCALAR_OP_H

/**
 * @file div_scalar_op.h
 * @brief DivScalar operator: INT32 output = INT32 input / scalar.
 *
 * Current implementation supports positive power-of-two scalar values.
 * The scalar is converted to a per-lane shift amount and executed with
 * LOGIC arithmetic right shift. IQ is issued to SMC0 and propagated by CRU
 * using smc_id_en.
 */

#include <cstdint>
#include <vector>
#include <string>
#include <random>

#include "../../hal/aiacc_smi_iq.h"
#include "../../iq/modules/ur_iq.h"
#include "../../iq/modules/rtovr_iq.h"
#include "../../iq/modules/logical_iq.h"
#include "../common/op_instructions_base.h"
#include "../common/op_base.h"
#include "../common/op_registry.h"
#include "../../app/app_json_parser.h"

namespace aiacc {
namespace op {

// ==========================================
// 数据类型枚举
// ==========================================

enum class DivScalarDataType : uint8_t {
    INT32 = 0  ///< INT32x4
};

// ==========================================
// DivScalar 算子参数
// ==========================================

struct DivScalarParams {
    // 张量维度
    uint32_t para_n{1};
    uint32_t para_c{1};
    uint32_t para_h{1};
    uint32_t para_w{4};

    // 数据类型
    DivScalarDataType para_dtype{DivScalarDataType::INT32};

    // UR 存储布局
    // input 张量
    uint8_t  para_input_bank{0};
    uint8_t  para_input_addr{0};

    // scalar (D)
    uint8_t  para_scalar_bank{1};
    uint8_t  para_scalar_addr{0};

    // output 张量
    uint8_t  para_output_bank{2};
    uint8_t  para_output_addr{0};

    int32_t  para_scalar_value{1};
    uint8_t  para_shift_amount{0};
    uint8_t  para_smc_id_en{7};

    // Elements per 128-bit UR word.
    uint32_t get_elems_per_word() const {
        return 4;
    }
};

// ==========================================
// DivScalar 算子微指令序�?// ==========================================

struct DivScalarInstructions : public OpInstructionsBase {
    std::vector<iq::UrCru> para_ur_read_crus;
    std::vector<iq::UrCru> para_ur_write_crus;
    std::vector<std::pair<uint8_t, uint8_t>> para_rtovr_routes;
    uint32_t para_compute_inst{0};
    uint16_t para_compute_code_bits{0};
    uint8_t para_smc_id_en{7};
    DivScalarDataType para_dtype{DivScalarDataType::INT32};

    uint32_t para_total_steps{0};
    uint32_t para_total_elements{0};
    uint32_t para_total_cycles{0};

    // 实现基类接口
    std::vector<IqInstructionFile> get_iq_files() const override;
    std::string get_operator_name() const override { return "DivScalar"; }
    uint32_t get_total_steps() const override { return para_total_steps; }
    uint32_t get_total_elements() const override { return para_total_elements; }
    std::vector<uint8_t> serialize() const override;
};

// ==========================================
// DivScalar 算子编译�?// ==========================================

class DivScalarOp : public OpBase {
public:
    DivScalarOp()  = default;
    ~DivScalarOp() = default;

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

    DivScalarInstructions compile(const DivScalarParams& params);
};

void print_div_scalar_instructions(const DivScalarInstructions& insts);

} // namespace op
} // namespace aiacc

#endif // DIV_SCALAR_OP_H

