#ifndef APP_JSON_PARSER_H
#define APP_JSON_PARSER_H

/**
 * @file app_json_parser.h
 * @brief App 层 JSON 解析器：解析算子 JSON 描述，转换为内部数据结构。
 *
 * 输入 JSON 格式示例：
 * {
 *   "operator": "FusedScaleAdd",
 *   "dimensions": { "N": 1, "C": 3, "H": 224, "W": 4 },
 *   "parameters": {
 *     "alpha": { "shape": [4], "dtype": "float32", "data": [...] }
 *   },
 *   "inputs": {
 *     "input_tensor": { "shape": [1,3,224,4], "dtype": "float32", "data": [...] },
 *     "other_tensor": { "shape": [1,3,224,4], "dtype": "float32", "data": [...] }
 *   }
 * }
 */

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>

namespace aiacc {
namespace app {

// ==========================================
// 1. JSON 解析后的数据结构
// ==========================================

/**
 * @brief 数据类型枚举
 */
enum class AppDataType {
    FLOAT32,
    INT32,
    FLOAT16,
    INT4,
    UNKNOWN
};

/**
 * @brief 张量描述（从 JSON 解析）
 */
struct AppTensorDesc {
    std::string para_name;                    ///< 张量名称
    std::vector<uint32_t> para_shape;         ///< 形状 [N, C, H, W] 或 [W]
    AppDataType para_dtype;                   ///< 数据类型
    std::vector<float> para_data_f32;         ///< float32 数据
    std::vector<int32_t> para_data_i32;       ///< int32 数据
};

/**
 * @brief 维度描述（从 JSON 解析）
 */
struct AppDimensions {
    uint32_t para_n{1};
    uint32_t para_c{1};
    uint32_t para_h{1};
    uint32_t para_w{1};
};

/**
 * @brief 算子请求（从 JSON 解析的完整结构）
 *
 * 可选 layout_mode：用于 Add 等算子的 UR bank 布局。
 * - 空或 "default"：分离布局（input=bank0, other=bank1, alpha=bank2, output=bank3），便于仿真对接。
 * - "bank_conflict"：同 bank 不同 addr 布局，用于测试 bank 冲突与多 slot 调度。
 */
struct AppOperatorRequest {
    std::string para_operator;                              ///< 算子名称
    AppDimensions para_dimensions;                          ///< 维度信息
    std::map<std::string, AppTensorDesc> para_parameters;   ///< 参数张量 (如 alpha)
    std::map<std::string, AppTensorDesc> para_inputs;       ///< 输入张量
    std::map<std::string, AppTensorDesc> para_outputs;      ///< 输出张量（可选）
    std::string para_layout_mode;                           ///< 可选。"default" 或空=分离布局；"bank_conflict"=冲突布局
    // bank_conflict 模式：每个 tensor 独立随机 bank（-1=未指定，由 seed 随机）
    int para_bc_input_bank{-1};                              ///< input tensor 所在 bank
    int para_bc_other_bank{-1};                              ///< other tensor 所在 bank
    int para_bc_alpha_bank{-1};                              ///< alpha tensor 所在 bank
    int para_bc_output_bank{-1};                             ///< output tensor 所在 bank
};

// ==========================================
// 2. JSON 解析器类
// ==========================================

/**
 * @brief JSON 解析器
 * 
 * 使用简单的手写解析器，避免外部依赖。
 * 支持的 JSON 子集：对象、数组、字符串、数字。
 */
class AppJsonParser {
public:
    AppJsonParser() = default;
    ~AppJsonParser() = default;

    /**
     * @brief 从 JSON 字符串解析算子请求
     * @param para_json_str JSON 字符串
     * @return 解析后的算子请求
     * @throws std::runtime_error 解析失败时抛出
     */
    AppOperatorRequest parse_operator_request(const std::string& para_json_str);

    /**
     * @brief 从文件加载并解析 JSON
     * @param para_file_path JSON 文件路径
     * @return 解析后的算子请求
     */
    AppOperatorRequest parse_from_file(const std::string& para_file_path);

    /**
     * @brief 将数据类型字符串转换为枚举
     */
    static AppDataType parse_dtype(const std::string& para_dtype_str);

    /**
     * @brief 将枚举转换为数据类型字符串
     */
    static std::string dtype_to_string(AppDataType para_dtype);

private:
    // 内部解析状态
    size_t para_pos_{0};
    std::string para_json_{};

    // 解析辅助函数
    void skip_whitespace();
    char peek_char();
    char consume_char();
    bool match_char(char para_expected);
    
    std::string parse_string();
    double parse_number();
    std::vector<double> parse_number_array();
    std::vector<uint32_t> parse_uint_array();
    
    void parse_object_start();
    void parse_object_end();
    bool parse_object_has_more();
    std::string parse_object_key();
    
    void parse_array_start();
    void parse_array_end();
    bool parse_array_has_more();

    // 高级解析函数
    AppTensorDesc parse_tensor_desc(const std::string& para_name);
    AppDimensions parse_dimensions();
    std::map<std::string, AppTensorDesc> parse_tensor_map();
};

// ==========================================
// 3. 辅助函数
// ==========================================

/**
 * @brief 打印算子请求信息（调试用）
 */
void print_operator_request(const AppOperatorRequest& para_req);

/**
 * @brief 验证算子请求的合法性
 * @return 空字符串表示合法，否则返回错误信息
 */
std::string validate_operator_request(const AppOperatorRequest& para_req);

} // namespace app
} // namespace aiacc

#endif // APP_JSON_PARSER_H
