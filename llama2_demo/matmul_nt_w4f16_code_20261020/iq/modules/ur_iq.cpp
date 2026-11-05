#include "ur_iq.h"
#include <cstring>

namespace aiacc {
namespace iq {

static inline void set_bits_u64(uint64_t& dst, uint32_t lsb, uint32_t width, uint64_t value) {
    if (width == 0) return;
    const uint64_t mask = (width >= 64) ? ~0ull : ((1ull << width) - 1ull) << lsb;
    dst = (dst & ~mask) | ((value << lsb) & mask);
}

void UrIq::pack_cru_ur_words(const UrCru& para_cru, uint32_t out_words[7]) {
    // 200 bits 需要用到 4 个 64-bit (256 bits capacity)
    uint64_t lo = 0;    // bits [63:0]
    uint64_t hi = 0;    // bits [127:64]
    uint64_t top = 0;   // bits [191:128]
    uint64_t top2 = 0;  // bits [199:192]
    
    uint32_t current_bit = 0;

    auto push_bits = [&](uint32_t width, uint64_t value) {
        uint64_t val_masked = value & ((width >= 64) ? ~0ull : (1ull << width) - 1ull);
        for (uint32_t i = 0; i < width; ++i) {
            uint32_t bit_pos = current_bit + i;
            uint64_t bit_val = (val_masked >> i) & 1ull;
            if (bit_pos < 64) {
                set_bits_u64(lo, bit_pos, 1, bit_val);
            } else if (bit_pos < 128) {
                set_bits_u64(hi, bit_pos - 64, 1, bit_val);
            } else if (bit_pos < 192) {
                set_bits_u64(top, bit_pos - 128, 1, bit_val);
            } else {
                set_bits_u64(top2, bit_pos - 192, 1, bit_val);
            }
        }
        current_bit += width;
    };

    // 1. 打包写端口 (4 * 33bit = 132 bits)
    // v0.4.18 顺序: we(1) -> wbank(3) -> waddr(8) -> wbe(16) -> smc_id_en(5)
    for (int i = 0; i < 4; ++i) {
        push_bits(1, para_cru.para_we[i] ? 1u : 0u);
        push_bits(3, (uint64_t)(para_cru.para_waddr[i].bank & 0x7u));
        push_bits(8, (uint64_t)(para_cru.para_waddr[i].addr_word & 0xFFu));
        push_bits(16, (uint64_t)para_cru.para_wbe[i]);
        push_bits(5, (uint64_t)(para_cru.para_w_smc_id_en[i] & 0x1Fu));
    }

    // 2. 打包读端口 (4 * 17bit = 68 bits)
    // v0.4.18 顺序: re(1) -> rbank(3) -> raddr(8) -> smc_id_en(5)
    for (int i = 0; i < 4; ++i) {
        push_bits(1, para_cru.para_re[i] ? 1u : 0u);
        push_bits(3, (uint64_t)(para_cru.para_raddr[i].bank & 0x7u));
        push_bits(8, (uint64_t)(para_cru.para_raddr[i].addr_word & 0xFFu));
        push_bits(5, (uint64_t)(para_cru.para_r_smc_id_en[i] & 0x1Fu));
    }

    // 输出映射: 200 bits -> 7 words (224 bits, 高 24 bits 为 0)
    out_words[0] = (uint32_t)(lo & 0xFFFFFFFFull);
    out_words[1] = (uint32_t)((lo >> 32) & 0xFFFFFFFFull);
    out_words[2] = (uint32_t)(hi & 0xFFFFFFFFull);
    out_words[3] = (uint32_t)((hi >> 32) & 0xFFFFFFFFull);
    out_words[4] = (uint32_t)(top & 0xFFFFFFFFull);
    out_words[5] = (uint32_t)((top >> 32) & 0xFFFFFFFFull);
    out_words[6] = (uint32_t)(top2 & 0xFFFFFFFFull);
}

void UrIq::make_micro_insts(const UrCru& para_cru, MicroInstruction out_insts[7]) {
    uint32_t words[7] = {0};
    pack_cru_ur_words(para_cru, words);
    for (int i = 0; i < 7; ++i) {
        out_insts[i].vld = para_cru.para_vld;
        out_insts[i].code = words[i];
    }
}

} // namespace iq
} // namespace aiacc
