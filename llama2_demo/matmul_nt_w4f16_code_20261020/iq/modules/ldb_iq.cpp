#include "ldb_iq.h"
#include <cstring>

namespace aiacc {
namespace iq {

namespace {

/**
 * @brief 将 value 的低 bit_len 位写入 words[] 的 [bit_pos, bit_pos+bit_len) 位置。
 *        支持跨 32-bit word 边界写入。
 */
void put_bits(uint32_t* words, int bit_pos, int bit_len, uint64_t value) {
    for (int i = 0; i < bit_len; ++i) {
        const int pos = bit_pos + i;
        const int word_idx = pos / 32;
        const int bit_in_word = pos % 32;
        const uint32_t bit_val = static_cast<uint32_t>((value >> i) & 1u);
        words[word_idx] = (words[word_idx] & ~(1u << bit_in_word)) |
                          (bit_val << bit_in_word);
    }
}

} // namespace

void LdbIq::pack_cru_ldb(const LdbCru& cru, uint32_t out_words[2]) {
    out_words[0] = 0;
    out_words[1] = 0;

    int pos = 0;
    put_bits(out_words, pos,  1, cru.para_vld ? 1u : 0u);                  pos += 1;   // [0]
    put_bits(out_words, pos, 15, cru.para_ur_addr & 0x7FFFu);              pos += 15;  // [15:1]
    put_bits(out_words, pos,  8, cru.para_ur_wr_line_cnt);                 pos += 8;   // [23:16]
    put_bits(out_words, pos,  9, cru.para_line_byte_cnt & 0x01FFu);        pos += 9;   // [32:24]  9b
    put_bits(out_words, pos,  6, cru.para_line_stride & 0x3Fu);            pos += 6;   // [38:33]
    put_bits(out_words, pos,  6, cru.para_const & 0x3Fu);                  pos += 6;   // [44:39]
    put_bits(out_words, pos,  1, cru.para_pre_fill ? 1u : 0u);             pos += 1;   // [45]
    put_bits(out_words, pos,  1, cru.para_post_fill ? 1u : 0u);            pos += 1;   // [46]
    put_bits(out_words, pos,  4, cru.para_inst_id & 0x0Fu);                pos += 4;   // [50:47]
    put_bits(out_words, pos,  5, cru.para_smc_id_en & 0x1Fu);              // [55:51]
}

void LdbIq::pack_cru_ldbaxi(const LdbAxiCru& cru, uint32_t out_words[4]) {
    out_words[0] = 0;
    out_words[1] = 0;
    out_words[2] = 0;
    out_words[3] = 0;

    int pos = 0;
    put_bits(out_words, pos,  1, cru.para_vld ? 1u : 0u);                          pos += 1;   // [0]
    put_bits(out_words, pos,  4, cru.para_first_byte_strb & 0x0Fu);                pos += 4;   // [4:1]
    put_bits(out_words, pos,  4, cru.para_last_byte_strb & 0x0Fu);                 pos += 4;   // [8:5]
    put_bits(out_words, pos,  4, cru.para_brst & 0x0Fu);                           pos += 4;   // [12:9]
    put_bits(out_words, pos, 40, cru.para_gr_base_addr & 0xFFFFFFFFFFULL);         pos += 40;  // [52:13]
    put_bits(out_words, pos, 40, cru.para_gr_intlv_addr & 0xFFFFFFFFFFULL);        pos += 40;  // [92:53]
    put_bits(out_words, pos,  4, cru.para_inst_id & 0x0Fu);                        pos += 4;   // [96:93]
    put_bits(out_words, pos,  5, cru.para_smc_id_en & 0x1Fu);                      pos += 5;   // [101:97]
    put_bits(out_words, pos,  2, static_cast<uint64_t>(cru.para_tid) & 0x03u);     // [103:102]
}

MicroInstruction LdbIq::make_micro_inst(const LdbCru& cru) {
    uint32_t w[2] = {0, 0};
    pack_cru_ldb(cru, w);
    MicroInstruction inst{};
    inst.vld  = cru.para_vld;
    inst.code = w[0];
    return inst;
}

MicroInstruction LdbIq::make_micro_inst(const LdbAxiCru& cru) {
    uint32_t w[4] = {0, 0, 0, 0};
    pack_cru_ldbaxi(cru, w);
    MicroInstruction inst{};
    inst.vld  = cru.para_vld;
    inst.code = w[0];
    return inst;
}

} // namespace iq
} // namespace aiacc
