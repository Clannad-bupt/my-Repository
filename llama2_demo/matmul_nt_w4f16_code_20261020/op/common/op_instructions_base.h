#ifndef OP_INSTRUCTIONS_BASE_H
#define OP_INSTRUCTIONS_BASE_H

/**
 * @file op_instructions_base.h
 * @brief 算子指令基类：提供统一的 IQ 文件输出接口
 * 
 * 设计理念：
 * - 每个算子的 Instructions 结构继承此基类
 * - 算子自己知道如何将指令按 IQ 分开输出
 * - TVGen 只需要调用统一的接口，不需要知道具体算子的细节
 */

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace aiacc {
namespace op {

/**
 * @brief IQ 指令文件描述
 */
struct IqInstructionFile {
    uint32_t para_iq_id;           ///< IQ 编号
    std::string para_iq_name;      ///< IQ 名称（如 "ur_read", "fpmul"）
    uint32_t para_width_bits;      ///< 指令位宽
    std::vector<uint8_t> para_data; ///< 指令数据（字节流）
    
    // 硬件循环支持（可选）
    bool para_use_loops{false};     ///< 是否使用硬件循环
    uint16_t para_loops{0};         ///< 循环次数
    uint8_t para_dly{0};            ///< 延迟周期
    int8_t para_inc{0};             ///< 地址增量
    
    std::string get_filename() const {
        return "iq_" + std::to_string(para_iq_id) + "_" + para_iq_name + ".bin";
    }

    std::string get_filename_no_ext() const {
        return "iq_" + std::to_string(para_iq_id) + "_" + para_iq_name;
    }
};

/**
 * @brief 算子指令基类
 * 
 * 所有算子的 Instructions 结构都应该继承此类
 */
class OpInstructionsBase {
public:
    virtual ~OpInstructionsBase() = default;
    
    /**
     * @brief 获取所有 IQ 指令文件
     * @return IQ 指令文件列表
     * 
     * 每个算子实现此方法，返回自己的 IQ 指令文件列表
     */
    virtual std::vector<IqInstructionFile> get_iq_files() const = 0;
    
    /**
     * @brief 获取算子名称
     */
    virtual std::string get_operator_name() const = 0;
    
    /**
     * @brief 获取统计信息
     */
    virtual uint32_t get_total_steps() const = 0;
    virtual uint32_t get_total_elements() const = 0;
    
    /**
     * @brief 序列化为单个字节流（向后兼容）
     */
    virtual std::vector<uint8_t> serialize() const = 0;
};

} // namespace op
} // namespace aiacc

#endif // OP_INSTRUCTIONS_BASE_H
