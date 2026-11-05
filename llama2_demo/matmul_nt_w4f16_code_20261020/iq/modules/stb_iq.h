#ifndef STB_IQ_H
#define STB_IQ_H

/**
 * @file stb_iq.h
 * @brief STB/STBAXI 微指令编码定义（ISA v0.4.18）
 *
 * STB 50 bit:
 *   vld[0], ur_addr[15:1], ur_rd_line_cnt[23:16], line_byte_cnt[32:24](9b),
 *   line_stride[38:33], inst_id[42:39], smc_id_en[47:43], tid[49:48]
 *
 * STBAXI 102 bit:
 *   vld[0], first_byte_strb[4:1], last_byte_strb[8:5], brst[12:9],
 *   gr_base_addr[52:13](40b), gr_intlv_addr[92:53](40b),
 *   inst_id[96:93], smc_id_en[101:97]
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

enum class StbTid : uint8_t {
    TID_0 = 0,
    TID_1 = 1,
    TID_2 = 2,
    TID_3 = 3,
};

struct StbCru {
    bool para_vld{false};
    uint16_t para_ur_addr{0};
    uint8_t para_ur_rd_line_cnt{1};
    uint16_t para_line_byte_cnt{0x0F};  // 9 bit
    uint8_t para_line_stride{1};
    uint8_t para_inst_id{0};
    uint8_t para_smc_id_en{7};
    StbTid para_tid{StbTid::TID_2};
};

struct StbAxiCru {
    bool para_vld{false};
    uint8_t para_first_byte_strb{0};
    uint8_t para_last_byte_strb{0};
    uint8_t para_brst{0x0F};
    uint64_t para_gr_base_addr{0};
    uint64_t para_gr_intlv_addr{0};
    uint8_t para_inst_id{0};
    uint8_t para_smc_id_en{7};
};

class StbIq : public SmiIqBase {
public:
    explicit StbIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static void pack_cru_stb(const StbCru& cru, uint32_t out_words[2]);
    static void pack_cru_stbaxi(const StbAxiCru& cru, uint32_t out_words[4]);
    static MicroInstruction make_micro_inst(const StbCru& cru);
    static MicroInstruction make_micro_inst(const StbAxiCru& cru);
};

} // namespace iq
} // namespace aiacc

#endif // STB_IQ_H
