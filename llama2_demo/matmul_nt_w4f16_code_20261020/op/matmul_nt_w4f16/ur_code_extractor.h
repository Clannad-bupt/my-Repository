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

// 从 UrCru 提取读端口的 CODE (17 bits)
// 仿真器 decode_ur_ctrl 约定：
//   [0]    = re
//   [3:1]  = bank
//   [11:4] = addr
//   [16:12]= smc_id_en
inline uint32_t extract_ur_read_port_code(const iq::UrCru& cru, int port) {
    if (port < 0 || port >= 4) {
        throw std::out_of_range("extract_ur_read_port_code: port must be in [0, 3]");
    }
    const uint32_t bank = cru.para_raddr[port].bank & 0x7u;
    const uint32_t addr = cru.para_raddr[port].addr_word & 0xFFu;
    const uint32_t re   = cru.para_re[port] ? 1u : 0u;

    const uint32_t smc_id_en = cru.para_r_smc_id_en[port] & 0x1Fu;

    uint32_t code = 0;
    code |= re;                  // bit 0
    code |= (bank << 1);         // bits 3:1
    code |= (addr << 4);         // bits 11:4
    code |= (smc_id_en << 12);   // bits 16:12
    return code;
}

inline uint32_t extract_ur_read_port0_code(const iq::UrCru& cru) {
    return extract_ur_read_port_code(cru, 0);
}
inline uint32_t extract_ur_read_port1_code(const iq::UrCru& cru) {
    return extract_ur_read_port_code(cru, 1);
}
inline uint32_t extract_ur_read_port2_code(const iq::UrCru& cru) {
    return extract_ur_read_port_code(cru, 2);
}
inline uint32_t extract_ur_read_port3_code(const iq::UrCru& cru) {
    return extract_ur_read_port_code(cru, 3);
}

// mm / mm_mac 兼容别名（与 Hongjun 侧命名一致）
inline uint32_t extract_ur_read_port_code17(const iq::UrCru& cru, int port) {
    return extract_ur_read_port_code(cru, port);
}

// ==========================================
// UR 写端口 CODE 提取（4个写端口）
// ==========================================

// 从 UrCru 提取写端口的 CODE (33 bits)
// 仿真器 decode_ur_write_ctrl 约定：
//   [0]     = we
//   [3:1]   = bank
//   [11:4]  = addr
//   [27:12] = wbe
//   [32:28] = smc_id_en
inline uint64_t extract_ur_write_port_code(const iq::UrCru& cru, int port) {
    if (port < 0 || port >= 4) {
        throw std::out_of_range("extract_ur_write_port_code: port must be in [0, 3]");
    }
    const uint64_t bank = cru.para_waddr[port].bank & 0x7;
    const uint64_t addr = cru.para_waddr[port].addr_word & 0xFF;
    const uint64_t we   = cru.para_we[port] ? 1u : 0u;
    const uint64_t wbe  = cru.para_wbe[port] & 0xFFFFu;

    const uint64_t smc_id_en = cru.para_w_smc_id_en[port] & 0x1Fu;

    uint64_t code = 0;
    code |= we;                 // bit 0
    code |= (bank << 1);        // bits 3:1
    code |= (addr << 4);        // bits 11:4
    code |= (wbe << 12);        // bits 27:12
    code |= (smc_id_en << 28);  // bits 32:28
    return code;
}

inline uint64_t extract_ur_write_port0_code(const iq::UrCru& cru) {
    return extract_ur_write_port_code(cru, 0);
}
inline uint64_t extract_ur_write_port1_code(const iq::UrCru& cru) {
    return extract_ur_write_port_code(cru, 1);
}
inline uint64_t extract_ur_write_port2_code(const iq::UrCru& cru) {
    return extract_ur_write_port_code(cru, 2);
}
inline uint64_t extract_ur_write_port3_code(const iq::UrCru& cru) {
    return extract_ur_write_port_code(cru, 3);
}

inline uint64_t extract_ur_write_port_code33(const iq::UrCru& cru, int port) {
    return extract_ur_write_port_code(cru, port);
}

} // namespace op
} // namespace aiacc

#endif // UR_CODE_EXTRACTOR_H
