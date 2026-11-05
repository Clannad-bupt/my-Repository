#ifndef UR_IQ_H
#define UR_IQ_H

/**
 * @file ur_iq.h
 * @brief UR 读写模块 IQ：支持带 wbe 和 smc_id_en 的 200-bit 控制字段打包。
 *
 * v0.4.18 格式:
 *   写端口 (x4): [we(1), wbank(3), waddr(8), wbe(16), smc_id_en(5)] = 33 bits
 *   读端口 (x4): [re(1), rbank(3), raddr(8), smc_id_en(5)]          = 17 bits
 *   总计: 4*33 + 4*17 = 200 bits，对齐 v4.17 RTL cru_ur_o[199:0]
 */

#include <cstdint>
#include "../smi_iq_base.h"
#include "../../op/common/aiacc_op_ur.h"

namespace aiacc {
namespace iq {

/**
 * @brief UR(CRU_UR) 控制字段结构体 (总共 200 bits)
 * - 写端口 (x4): [we(1), wbank(3), waddr(8), wbe(16), smc_id_en(5)] -> 每组 33 bits
 * - 读端口 (x4): [re(1), rbank(3), raddr(8), smc_id_en(5)]          -> 每组 17 bits
 */
struct UrCru {
    bool para_vld{false};

    // 写端口
    bool para_we[4]{false};
    aiacc::op::UrAddr para_waddr[4]{};
    uint16_t para_wbe[4]{0};
    uint8_t para_w_smc_id_en[4]{31, 31, 31, 31};  // 每个写端口独立的 smc_id_en，默认31=全使能

    // 读端口
    bool para_re[4]{false};
    aiacc::op::UrAddr para_raddr[4]{};
    uint8_t para_r_smc_id_en[4]{31, 31, 31, 31};  // 每个读端口独立的 smc_id_en，默认31=全使能
};

class UrIq : public SmiIqBase {
public:
    explicit UrIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    // 打包成 7 个 32-bit words (200 bits 占用 words[0..6])
    static void pack_cru_ur_words(const UrCru& para_cru, uint32_t out_words[7]);

    static void make_micro_insts(const UrCru& para_cru, MicroInstruction out_insts[7]);
};

} // namespace iq
} // namespace aiacc

#endif
