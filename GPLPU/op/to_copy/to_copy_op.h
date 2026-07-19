#ifndef TO_COPY_OP_H
#define TO_COPY_OP_H

/**
 * @file to_copy_op.h
 * @brief _to_copy 算子 —— 对应 PyTorch aten::_to_copy / cast 转换
 *
 * 支持不同数据类型之间的拷贝与转换：
 * - 同数据类型拷贝（FP32->FP32, FP16->FP16, INT32->INT32）：使用 LOGICAL (COPY)
 * - 浮点与浮点转换（FP32 <-> FP16）：使用 FPtoFP
 * - 浮点与整数转换（FP <-> INT）：使用 FPtoInt / INTtoFP
 */

#include <cstdint>
#include <vector>
#include <string>
#include <random>

#include "../../hal/aiacc_smi_iq.h"
#include "../../iq/modules/ur_iq.h"
#include "../../iq/modules/rtovr_iq.h"
#include "../../iq/modules/logical_iq.h"
#include "../../iq/modules/fptofp_iq.h"
#include "../../iq/modules/fptoint_iq.h"
#include "../../iq/modules/inttofp_iq.h"
#include "../../iq/modules/inttoint_iq.h"
#include "../common/op_instructions_base.h"
#include "../common/op_base.h"
#include "../common/op_registry.h"
#include "../../app/app_json_parser.h"

namespace aiacc {
namespace op {

// ==========================================
// Bank 冲突模式
// ==========================================
enum class ToCopyBankMode : uint8_t {
    DIFF_BANK = 0,
    SAME_BANK = 1
};

// ==========================================
// ToCopy 算子参数
// ==========================================
struct ToCopyParams {
    uint32_t para_n{1};
    uint32_t para_c{1};
    uint32_t para_h{1};
    uint32_t para_w{4};

    // 输入与输出数据类型
    app::AppDataType para_in_dtype{app::AppDataType::FLOAT32};
    app::AppDataType para_out_dtype{app::AppDataType::FLOAT32};

    // UR 物理地址
    uint8_t para_input_bank{0};
    uint8_t para_input_addr{0};
    uint8_t para_output_bank{1};
    uint8_t para_output_addr{0};

    uint32_t get_input_elems_per_word() const {
        return (para_in_dtype == app::AppDataType::FLOAT16) ? 8 : 4;
    }

    uint32_t get_output_elems_per_word() const {
        return (para_out_dtype == app::AppDataType::FLOAT16) ? 8 : 4;
    }

    bool is_same_bank() const {
        return para_input_bank == para_output_bank;
    }

    ToCopyBankMode get_bank_mode() const {
        return is_same_bank() ? ToCopyBankMode::SAME_BANK : ToCopyBankMode::DIFF_BANK;
    }
};

// ==========================================
// ToCopy 编译后的微指令序列
// ==========================================
struct ToCopyInstructions : public OpInstructionsBase {
    std::vector<iq::UrCru> para_ur_read_crus;
    std::vector<iq::UrCru> para_ur_write_crus;

    // RtoVR 路由
    std::vector<std::pair<uint8_t, uint8_t>> para_rtovr_routes;

    // 计算单元类型和控制指令
    uint32_t para_compute_iq_id{11}; // 默认 LOGIC IQ
    std::string para_compute_iq_name{"logic"};
    uint8_t para_compute_inst{0};
    uint32_t para_code_bits{6};

    // 数据类型与统计
    app::AppDataType para_in_dtype{app::AppDataType::FLOAT32};
    app::AppDataType para_out_dtype{app::AppDataType::FLOAT32};
    uint32_t para_total_steps{0};
    uint32_t para_total_elements{0};
    uint32_t para_total_cycles{0};

    // Bank 信息
    ToCopyBankMode para_bank_mode{ToCopyBankMode::DIFF_BANK};
    uint8_t para_input_bank{0};
    uint8_t para_output_bank{1};
    uint8_t para_input_addr{0};
    uint8_t para_output_addr{0};

    // 实现基类虚接口
    std::vector<IqInstructionFile> get_iq_files() const override;
    std::string get_operator_name() const override { return "_to_copy"; }
    uint32_t get_total_steps() const override { return para_total_steps; }
    uint32_t get_total_elements() const override { return para_total_elements; }
    std::vector<uint8_t> serialize() const override;
};

// ==========================================
// ToCopy 算子类
// ==========================================
class ToCopyOp : public OpBase {
public:
    ToCopyOp() = default;
    ~ToCopyOp() = default;

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

    ToCopyInstructions compile(const ToCopyParams& params);
};

} // namespace op
} // namespace aiacc

#endif // TO_COPY_OP_H
