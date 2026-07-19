#ifndef UR_CODE_EXTRACTOR_H
#define UR_CODE_EXTRACTOR_H

/**
 * @file ur_code_extractor.h
 * @brief UR 指令 CODE 字段提取工具
 */

#include <cstdint>
#include <stdexcept>
#include "../../iq/modules/ur_iq.h"

namespace aiacc {
namespace op {

// ==========================================
// UR 读端口 CODE 提取（4个读端口）
// ==========================================

// 从 UrCru 提取读端口的 CODE (12 bits)
// 注意：这里直接生成 **仿真器期望的 LSB 位序**。
// 仿真器 decode_ur_ctrl 约定：
//   [0]    = re
//   [3:1]  = bank
//   [11:4] = addr
inline uint32_t extract_ur_read_port_code(const iq::UrCru& cru, int port) {
    if (port < 0 || port >= 4) {
        throw std::out_of_range("extract_ur_read_port_code: port must be in [0, 3]");
    }
    const uint32_t bank = cru.para_raddr[port].bank & 0x7u;
    const uint32_t addr = cru.para_raddr[port].addr_word & 0xFFu;
    const uint32_t re   = cru.para_re[port] ? 1u : 0u;

    uint32_t code = 0;
    code |= re;            // bit 0
    code |= (bank << 1);   // bits 3:1
    code |= (addr << 4);   // bits 11:4
    return code;
}

// 便捷函数：提取读端口0的 CODE
inline uint32_t extract_ur_read_port0_code(const iq::UrCru& cru) {
    return extract_ur_read_port_code(cru, 0);
}

// 便捷函数：提取读端口1的 CODE
inline uint32_t extract_ur_read_port1_code(const iq::UrCru& cru) {
    return extract_ur_read_port_code(cru, 1);
}

// 便捷函数：提取读端口2的 CODE
inline uint32_t extract_ur_read_port2_code(const iq::UrCru& cru) {
    return extract_ur_read_port_code(cru, 2);
}

// 便捷函数：提取读端口3的 CODE
inline uint32_t extract_ur_read_port3_code(const iq::UrCru& cru) {
    return extract_ur_read_port_code(cru, 3);
}

// ==========================================
// UR 写端口 CODE 提取（4个写端口）
// ==========================================

// 从 UrCru 提取写端口的 CODE (28 bits)
// 注意：这里直接生成 **仿真器期望的 LSB 位序**，而不是 SMI_IQ 内部位序。
// 仿真器 decode_ur_write_ctrl 约定：
//   [0]    = we
//   [3:1]  = bank
//   [11:4] = addr
//   [27:12]= wbe
inline uint32_t extract_ur_write_port_code(const iq::UrCru& cru, int port) {
    if (port < 0 || port >= 4) {
        throw std::out_of_range("extract_ur_write_port_code: port must be in [0, 3]");
    }
    const uint32_t bank = cru.para_waddr[port].bank & 0x7;
    const uint32_t addr = cru.para_waddr[port].addr_word & 0xFF;
    const uint32_t we   = cru.para_we[port] ? 1u : 0u;
    const uint32_t wbe  = cru.para_wbe[port] & 0xFFFFu;

    uint32_t code = 0;
    code |= we;              // bit 0
    code |= (bank << 1);     // bits 3:1
    code |= (addr << 4);     // bits 11:4
    code |= (wbe << 12);     // bits 27:12
    return code;
}

// 便捷函数：提取写端口0的 CODE
inline uint32_t extract_ur_write_port0_code(const iq::UrCru& cru) {
    return extract_ur_write_port_code(cru, 0);
}

// 便捷函数：提取写端口1的 CODE
inline uint32_t extract_ur_write_port1_code(const iq::UrCru& cru) {
    return extract_ur_write_port_code(cru, 1);
}

// 便捷函数：提取写端口2的 CODE
inline uint32_t extract_ur_write_port2_code(const iq::UrCru& cru) {
    return extract_ur_write_port_code(cru, 2);
}

// 便捷函数：提取写端口3的 CODE
inline uint32_t extract_ur_write_port3_code(const iq::UrCru& cru) {
    return extract_ur_write_port_code(cru, 3);
}

} // namespace op
} // namespace aiacc

#endif // UR_CODE_EXTRACTOR_H
