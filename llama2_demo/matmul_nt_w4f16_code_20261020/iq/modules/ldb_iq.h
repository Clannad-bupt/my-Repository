#ifndef LDB_IQ_H
#define LDB_IQ_H

/**
 * @file ldb_iq.h
 * @brief LDB/LDBAXI 微指令编码定义（ISA v0.4.18）
 *
 * LDB 56 bit:
 *   vld[0], ur_addr[15:1], ur_wr_line_cnt[23:16], line_byte_cnt[32:24](9b),
 *   line_stride[38:33], const[44:39], pre_fill[45], post_fill[46],
 *   inst_id[50:47], smc_id_en[55:51]
 *
 * LDBAXI 104 bit:
 *   vld[0], first_byte_strb[4:1], last_byte_strb[8:5], brst[12:9],
 *   gr_base_addr[52:13](40b), gr_intlv_addr[92:53](40b),
 *   inst_id[96:93], smc_id_en[101:97], tid[103:102]
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

enum class LdbAxiTid : uint8_t {
    TID_0 = 0,
    TID_1 = 1,
    TID_2 = 2,
    TID_3 = 3,
};

struct LdbCru {
    bool para_vld{false};
    uint16_t para_ur_addr{0};
    uint8_t para_ur_wr_line_cnt{1};
    uint16_t para_line_byte_cnt{0x0F};  // 9 bit
    uint8_t para_line_stride{1};
    uint8_t para_const{0};
    bool para_pre_fill{false};
    bool para_post_fill{false};
    uint8_t para_inst_id{0};
    uint8_t para_smc_id_en{7};
};

struct LdbAxiCru {
    bool para_vld{false};
    uint8_t para_first_byte_strb{0};
    uint8_t para_last_byte_strb{0};
    uint8_t para_brst{0x0F};
    uint64_t para_gr_base_addr{0};
    uint64_t para_gr_intlv_addr{0};
    uint8_t para_inst_id{0};
    uint8_t para_smc_id_en{7};
    LdbAxiTid para_tid{LdbAxiTid::TID_0};
};

class LdbIq : public SmiIqBase {
public:
    explicit LdbIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    static void pack_cru_ldb(const LdbCru& cru, uint32_t out_words[2]);
    static void pack_cru_ldbaxi(const LdbAxiCru& cru, uint32_t out_words[4]);
    static MicroInstruction make_micro_inst(const LdbCru& cru);
    static MicroInstruction make_micro_inst(const LdbAxiCru& cru);
};

} // namespace iq
} // namespace aiacc

#endif // LDB_IQ_H
