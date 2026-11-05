#include "ds_iq.h"
#include <cstring>

namespace aiacc {
namespace iq {

namespace {

inline void put_bits(uint32_t* words, int bit_pos, int bit_len, uint64_t value) {
    const uint64_t mask = (bit_len == 64) ? ~uint64_t(0) : ((uint64_t(1) << bit_len) - 1);
    value &= mask;
    int w_lo = bit_pos / 32;
    int off  = bit_pos % 32;
    int room = 32 - off;
    if (bit_len <= room) {
        words[w_lo] |= static_cast<uint32_t>(value << off);
    } else {
        words[w_lo]     |= static_cast<uint32_t>(value << off);
        uint64_t rest    = value >> room;
        int remaining    = bit_len - room;
        int w_hi         = w_lo + 1;
        while (remaining > 0) {
            int take = (remaining >= 32) ? 32 : remaining;
            uint32_t chunk = static_cast<uint32_t>(rest & 0xFFFFFFFFu);
            if (take < 32) {
                chunk &= ((1u << take) - 1u);
            }
            words[w_hi] |= chunk;
            rest >>= 32;
            remaining -= 32;
            ++w_hi;
        }
    }
}

} // namespace

// ==========================================================================
// DS (Table 5.83)
// ==========================================================================
void DsIq::pack_cru_ds(const DsCru& para_cru, uint32_t out_words[2]) {
    std::memset(out_words, 0, 2 * sizeof(uint32_t));

    put_bits(out_words,  0, 1, para_cru.para_vld ? 1u : 0u);
    put_bits(out_words,  1, 1, static_cast<uint8_t>(para_cru.para_op_type)   & 0x1u);
    put_bits(out_words,  2, 2, static_cast<uint8_t>(para_cru.para_data_type) & 0x3u);
    put_bits(out_words,  4, 8, para_cru.para_r_in_cnt    & 0xFFu);
    put_bits(out_words, 12, 8, para_cru.para_r_out_cnt   & 0xFFu);
    put_bits(out_words, 20, 5, para_cru.para_t_dim0_cnt  & 0x1Fu);
    put_bits(out_words, 25, 5, para_cru.para_t_dim1_cnt  & 0x1Fu);
    put_bits(out_words, 30, 4, para_cru.para_inst_id     & 0xFu);
    put_bits(out_words, 34, 5, para_cru.para_smc_id_en   & 0x1Fu);
}

MicroInstruction DsIq::make_micro_inst(const DsCru& para_cru) {
    uint32_t w[2] = {0, 0};
    pack_cru_ds(para_cru, w);
    MicroInstruction inst{};
    inst.vld  = para_cru.para_vld;
    inst.code = w[0];  // DS 39 bit 整体可放在 (w[0], w[1] 低 7 bit)；全量请用 pack_cru_ds
    return inst;
}

DsCru DsIq::make_reshape(DsDataType dtype,
                         uint8_t    in_elems_per_line,
                         uint8_t    out_elems_per_line,
                         uint8_t    inst_id,
                         uint8_t    smc_id_en) {
    DsCru cru{};
    cru.para_vld         = true;
    cru.para_op_type     = DsOpType::RESHAPE;
    cru.para_data_type   = dtype;
    cru.para_r_in_cnt    = in_elems_per_line;
    cru.para_r_out_cnt   = out_elems_per_line;
    cru.para_t_dim0_cnt  = 0;
    cru.para_t_dim1_cnt  = 0;
    cru.para_inst_id     = inst_id   & 0xFu;
    cru.para_smc_id_en   = smc_id_en & 0x1Fu;
    return cru;
}

DsCru DsIq::make_transpose(DsDataType dtype,
                           uint8_t    dim0_size,
                           uint8_t    dim1_size,
                           uint8_t    inst_id,
                           uint8_t    smc_id_en) {
    // ISA: 字段值 = size - 1；size 合法范围 [1, 32]。
    uint8_t d0 = (dim0_size == 0) ? 0 : static_cast<uint8_t>((dim0_size - 1) & 0x1Fu);
    uint8_t d1 = (dim1_size == 0) ? 0 : static_cast<uint8_t>((dim1_size - 1) & 0x1Fu);

    DsCru cru{};
    cru.para_vld         = true;
    cru.para_op_type     = DsOpType::TRANSPOSE;
    cru.para_data_type   = dtype;
    cru.para_r_in_cnt    = 0;
    cru.para_r_out_cnt   = 0;
    cru.para_t_dim0_cnt  = d0;
    cru.para_t_dim1_cnt  = d1;
    cru.para_inst_id     = inst_id   & 0xFu;
    cru.para_smc_id_en   = smc_id_en & 0x1Fu;
    return cru;
}

} // namespace iq
} // namespace aiacc
