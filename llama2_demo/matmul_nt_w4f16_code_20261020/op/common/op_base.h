#ifndef OP_BASE_H
#define OP_BASE_H

/**
 * @file op_base.h
 * @brief 算子基类：定义所有算子必须实现的统一接口
 * 
 * 设计理念：
 * - 所有算子继承此基类，实现统一的接口
 * - TVGen 通过此基类动态调用算子，无需硬编码算子类型
 * - 算子自己负责编译、数据生成和golden计算
 */

#include <string>
#include <vector>
#include <random>
#include "../../app/app_json_parser.h"
#include "op_instructions_base.h"

namespace aiacc {
namespace op {

/**
 * @brief 算子基类
 * 
 * 所有算子必须继承此类，并实现所有纯虚函数
 */
class OpBase {
public:
    virtual ~OpBase() = default;
    
    /**
     * @brief 获取算子名称
     * @return 算子名称字符串
     */
    virtual std::string get_name() const = 0;
    
    /**
     * @brief 编译算子，生成指令
     * @param req 算子请求参数
     * @return 指令对象指针，调用者负责释放
     */
    virtual OpInstructionsBase* compile(const app::AppOperatorRequest& req) = 0;
    
    /**
     * @brief 计算golden结果
     * @param req 算子请求参数
     * @param golden_bin 输出golden结果的二进制数据
     * @return 成功返回true，失败返回false
     */
    virtual bool calculate_golden(const app::AppOperatorRequest& req, 
                                 std::vector<uint8_t>& golden_bin) = 0;
    
/**
     * @brief 生成二进制数据 (序列化)
     * @note  读取 req 里的数据并转换为二进制
     */
    virtual bool generate_data(const app::AppOperatorRequest& req, 
                              std::vector<uint8_t>& input_bin, 
                              std::vector<uint8_t>& other_bin, 
                              std::vector<uint8_t>& alpha_bin) = 0;

/**
     * @brief [新增接口] 填充随机请求参数 (造数据)
     * @details 算子根据自己的特性，往 req 里填充 input_tensor, other_tensor, alpha 等
     * @param req 请求对象 (引用，算子负责修改它)
     * @param rng 随机数引擎 (引用)
     * @param data_dist 通用数据分布 (方便生成 Tensor)
     */
    virtual void fill_random_request(app::AppOperatorRequest& req, 
                                     std::mt19937& rng,
                                     std::uniform_real_distribution<float>& data_dist,
                                     const std::string& dtype_str) = 0;
};


} // namespace op
} // namespace aiacc

#endif // OP_BASE_H