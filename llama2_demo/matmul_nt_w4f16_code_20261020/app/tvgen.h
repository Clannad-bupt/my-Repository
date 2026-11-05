#ifndef TVGEN_H
#define TVGEN_H

/**
 * @file tvgen.h
 * @brief TVGen 测试向量生成器：Compiler + DataGen + GoldenCalc
 */

#include <cstdint>
#include <string>
#include <vector>
#include <map>

#include "app_json_parser.h"
#include "../op/common/op_instructions_base.h"

// 前向声明
namespace aiacc {
namespace op {
    class OpInstructionsBase;
}
}

namespace aiacc {
namespace app {

struct TvgenConfig {
    std::string para_output_dir{"out"};
    bool para_verbose{false};
    std::map<std::string, std::vector<int32_t>> para_int_overrides; // Random 模式下覆盖算子 int 参数，如 newton_iters
};

struct TvgenResult {
    bool para_success{false};
    std::string para_error_msg;
    
    std::string para_input_bin_path;
    std::string para_other_bin_path;
    std::string para_alpha_bin_path;
    std::string para_golden_bin_path;
    std::string para_metadata_path;
    
    std::vector<std::string> para_iq_inst_paths;  // IQ 指令文件路径列表
    
    uint32_t para_total_insts{0};
    uint32_t para_total_elements{0};
};

class Tvgen {
public:
    explicit Tvgen(const TvgenConfig& para_config = TvgenConfig{});
    ~Tvgen() = default;

    TvgenResult generate(const AppOperatorRequest& para_req);
    TvgenResult generate_from_json(const std::string& para_json_str);
    TvgenResult generate_from_file(const std::string& para_file_path);
    TvgenResult generate_random(uint32_t para_seed, 
                               const std::string& para_operator = "", 
                               const std::string& para_dtype = "fp32",
                               const std::string& para_layout = "");

private:
    TvgenConfig para_config_;

    bool compile(const AppOperatorRequest& para_req,
                 std::vector<uint8_t>& para_inst_bin,
                 std::vector<op::IqInstructionFile>& para_iq_files);

    bool generate_data(const AppOperatorRequest& para_req,
                       std::vector<uint8_t>& para_input_bin,
                       std::vector<uint8_t>& para_other_bin,
                       std::vector<uint8_t>& para_alpha_bin);
    bool calculate_golden(const AppOperatorRequest& para_req, std::vector<uint8_t>& para_golden_bin);
    bool write_bin_file(const std::string& para_path, const std::vector<uint8_t>& para_data);
    bool write_hex_file(const std::string& para_path, const std::vector<uint8_t>& para_data);
};

} // namespace app
} // namespace aiacc

#endif
