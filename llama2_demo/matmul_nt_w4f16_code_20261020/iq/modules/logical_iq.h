#ifndef LOGICAL_IQ_H
#define LOGICAL_IQ_H

/**
 * @file logical_iq.h
 * @brief LOGICAL 模块 IQ：逻辑运算单元
 * 
 * 对齐 v4.17 RTL:
 * i_cru_logic[10:0] = {smc_id_en[4:0], precision_mode, opcode[3:0], inst_valid}
 * 
 * 支持 16 种逻辑操作：AND, OR, XOR, NOT, COPY, SELECT_GT/EQ/LS, SHIFT 等
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

// 逻辑操作码
enum class LogicalOp : uint8_t {
    AND = 0x0,
    OR = 0x1,
    XOR = 0x2,
    NOT = 0x3,
    COPY = 0x4,
    SELECT_GREAT = 0x5,
    SELECT_EQUAL = 0x6,
    SELECT_LESS = 0x7,
    LOGIC_LEFT_SHIFT = 0x8,
    // ... 其他操作
};

struct LogicalCru {
    bool para_inst_valid{false};
    bool para_precision_32b{true};  // 1=32bit(4通道), 0=16bit(8通道)
    LogicalOp para_opcode{LogicalOp::AND};
    uint8_t para_smc_id_en{31};     // 5-bit, 31 means broadcast to all SMCs
};

class LogicalIq : public SmiIqBase {
public:
    explicit LogicalIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static uint16_t pack_cru_logical(const LogicalCru& para_cru);
    static MicroInstruction make_micro_inst(const LogicalCru& para_cru);
};

} // namespace iq
} // namespace aiacc

#endif
