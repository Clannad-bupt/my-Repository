#ifndef LSU_EMIT_H
#define LSU_EMIT_H

/**
 * @file lsu_emit.h
 * @brief Load/Store IQ 发码公共能力（供任意算子复用）
 */

#include <cstdint>
#include <vector>

#include "../../iq/modules/ldb_iq.h"
#include "../../iq/modules/stb_iq.h"
#include "op_instructions_base.h"

namespace aiacc {
namespace op {

struct LsuLoadParams {
    // LDBAXI (104b)
    uint64_t para_gr_base_addr{0};
    uint64_t para_gr_intlv_addr{0};
    uint8_t para_brst{0xF};
    uint8_t para_first_byte_strb{0};
    uint8_t para_last_byte_strb{0};
    uint8_t para_tid{0};

    // LDB (56b)
    uint16_t para_ur_addr{0};
    uint8_t para_ur_wr_line_cnt{1};
    uint16_t para_line_byte_cnt{0x0F};   // 9 bit
    uint8_t para_line_stride{1};
    uint8_t para_const{0};
    bool para_pre_fill{false};
    bool para_post_fill{false};

    // Shared
    uint8_t para_smc_id_en{7};
    uint8_t para_inst_id{0};

    // 13-bit INC shell
    uint16_t para_ldb_loops{0x00FF};
    uint16_t para_ldbaxi_loops{0x000F};
    uint16_t para_ldb_inc{0x0010};
    uint16_t para_ldbaxi_inc{0x0100};
    uint8_t para_ldb_dly{0};
    uint8_t para_ldbaxi_dly{0};
    uint8_t para_ldb_cond{0};
    uint8_t para_ldbaxi_cond{0};
    uint8_t para_ldb_embed{0};
    uint8_t para_ldbaxi_embed{0};
    uint8_t para_ldb_embed_end{0};
    uint8_t para_ldbaxi_embed_end{0};
    uint8_t para_ldb_inc_embed{0};
    uint8_t para_ldbaxi_inc_embed{0};
    bool para_ldb_vld{true};
    bool para_ldbaxi_vld{true};
    bool para_ldb_jump{false};
    bool para_ldbaxi_jump{false};
};

struct LsuStoreParams {
    // STBAXI (102b)
    uint64_t para_gr_base_addr{0};
    uint64_t para_gr_intlv_addr{0};
    uint8_t para_brst{0xF};
    uint8_t para_first_byte_strb{0};
    uint8_t para_last_byte_strb{0};

    // STB (50b)
    uint16_t para_ur_addr{0};
    uint8_t para_ur_rd_line_cnt{1};
    uint16_t para_line_byte_cnt{0x0F};  // 9 bit
    uint8_t para_line_stride{1};
    uint8_t para_tid{2};

    // Shared
    uint8_t para_smc_id_en{7};
    uint8_t para_inst_id{0};

    // 13-bit INC shell
    uint16_t para_stb_loops{0x00FF};
    uint16_t para_stbaxi_loops{0x000F};
    uint16_t para_stb_inc{0x0010};
    uint16_t para_stbaxi_inc{0x0100};
    uint8_t para_stb_dly{0};
    uint8_t para_stbaxi_dly{0};
    uint8_t para_stb_cond{0};
    uint8_t para_stbaxi_cond{0};
    uint8_t para_stb_embed{0};
    uint8_t para_stbaxi_embed{0};
    uint8_t para_stb_embed_end{0};
    uint8_t para_stbaxi_embed_end{0};
    uint8_t para_stb_inc_embed{0};
    uint8_t para_stbaxi_inc_embed{0};
    bool para_stb_vld{true};
    bool para_stbaxi_vld{true};
    bool para_stb_jump{false};
    bool para_stbaxi_jump{false};
};

std::vector<IqInstructionFile> emit_load_iq_files(const LsuLoadParams& params);
std::vector<IqInstructionFile> emit_store_iq_files(const LsuStoreParams& params);

/** vld=1、code=0 的 NOP，占用 idle_cycles 个 IQ 周期；width_bits 须与目标队列一致。 */
std::vector<uint8_t> make_iq_idle_instruction(uint32_t width_bits, uint16_t idle_cycles);

void fill_ldbaxi_cru(const LsuLoadParams& params, iq::LdbAxiCru& cru);
void fill_ldb_cru(const LsuLoadParams& params, iq::LdbCru& cru);
void fill_stb_cru(const LsuStoreParams& params, iq::StbCru& cru);
void fill_stbaxi_cru(const LsuStoreParams& params, iq::StbAxiCru& cru);

} // namespace op
} // namespace aiacc

#endif // LSU_EMIT_H
