#ifndef SMI_IQ_BASE_H
#define SMI_IQ_BASE_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "../hal/aiacc_smi_iq.h"

namespace aiacc {
namespace iq {

/**
 * @file smi_iq_base.h
 * @brief IQ 基类声明：提供微指令 push、字段位宽检查、队列容量保护，以及可选的软件 sink。
 */

using aiacc::hal::MicroInstruction;
using aiacc::hal::SmiIqHandle;

/**
 * @brief SMI-IQ 基类
 * [Design Rationale]: 
 * 统一封装所有 IQ 都共有的能力：
 * 1) 基于 RTL 参数(config)的位宽检查
 * 2) 指令 push（先缓存到软件队列，便于测试；未来可扩展 flush 到真实寄存器接口）
 * 3) 队列深度检查（防止写爆 instq/addrq/dataq）
 */
class SmiIqBase {
public:
    explicit SmiIqBase(SmiIqHandle* para_handle);

    virtual ~SmiIqBase() = default;

    SmiIqHandle* handle() const { return para_handle_; }

    // 将一条微指令推进软件侧队列（不等于写寄存器，只是“生成结果”）。
    void push_micro_inst(const MicroInstruction& para_inst);

    // 获取已生成的全部微指令（用于单元测试/调试）。
    const std::vector<MicroInstruction>& micro_insts() const { return micro_insts_; }

protected:
    void check_micro_inst_fields_or_throw(const MicroInstruction& para_inst) const;
    void check_instq_capacity_or_throw(uint32_t para_new_count) const;

private:
    SmiIqHandle* para_handle_{nullptr};
    std::vector<MicroInstruction> micro_insts_{};
};

} // namespace iq
} // namespace aiacc

#endif
