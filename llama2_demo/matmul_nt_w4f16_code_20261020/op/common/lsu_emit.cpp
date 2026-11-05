#include "lsu_emit.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "../../iq/iq_id_defs.h"

namespace aiacc {
namespace op {

namespace detail {

constexpr uint32_t BITS_EMBED = 3;
constexpr uint32_t BITS_EMBED_END = 6;
constexpr uint32_t BITS_LOOPS = 10;
constexpr uint32_t BITS_COND = 7;
constexpr uint32_t BITS_DLY = 3;
constexpr uint32_t BITS_INC = 13;
constexpr uint32_t BITS_INC_EMBED = 6;

constexpr uint32_t MASK_EMBED = (1u << BITS_EMBED) - 1;
constexpr uint32_t MASK_EMBED_END = (1u << BITS_EMBED_END) - 1;
constexpr uint32_t MASK_LOOPS = (1u << BITS_LOOPS) - 1;
constexpr uint32_t MASK_COND = (1u << BITS_COND) - 1;
constexpr uint32_t MASK_DLY = (1u << BITS_DLY) - 1;
constexpr uint32_t MASK_INC = (1u << BITS_INC) - 1;
constexpr uint32_t MASK_INC_EMBED = (1u << BITS_INC_EMBED) - 1;

uint64_t serialize_header_bits(bool vld,
                               bool jump,
                               uint8_t embed,
                               uint16_t loops,
                               uint8_t cond,
                               uint8_t dly,
                               uint16_t inc,
                               uint8_t embed_end_bits,
                               uint8_t inc_embed_bits) {
    uint64_t bits = 0;
    uint32_t pos = 0;
    const uint8_t embed_end = (embed == 0) ? (embed_end_bits & detail::MASK_EMBED_END) : 0u;
    const uint8_t inc_embed = (embed == 0) ? (inc_embed_bits & detail::MASK_INC_EMBED) : 0u;
    bits |= (static_cast<uint64_t>(inc_embed) & detail::MASK_INC_EMBED) << pos;
    pos += BITS_INC_EMBED;
    bits |= (static_cast<uint64_t>(inc) & detail::MASK_INC) << pos;
    pos += BITS_INC;
    bits |= (static_cast<uint64_t>(dly) & detail::MASK_DLY) << pos;
    pos += BITS_DLY;
    bits |= (static_cast<uint64_t>(cond) & detail::MASK_COND) << pos;
    pos += BITS_COND;
    bits |= (static_cast<uint64_t>(loops) & detail::MASK_LOOPS) << pos;
    pos += BITS_LOOPS;
    bits |= (static_cast<uint64_t>(embed_end) & detail::MASK_EMBED_END) << pos;
    pos += BITS_EMBED_END;
    bits |= (static_cast<uint64_t>(embed) & detail::MASK_EMBED) << pos;
    pos += BITS_EMBED;
    bits |= (static_cast<uint64_t>(jump ? 1u : 0u) & 1u) << pos;
    pos += 1;
    bits |= (static_cast<uint64_t>(vld ? 1u : 0u) & 1u) << pos;
    return bits;
}

std::vector<uint8_t> serialize_code_words_lsb_first(const uint32_t* words,
                                                    uint32_t code_bits) {
    const uint32_t num_bytes = (code_bits + 7u) / 8u;
    std::vector<uint8_t> bytes(num_bytes, 0);
    for (uint32_t i = 0; i < num_bytes; ++i) {
        const uint32_t wi = i / 4;
        const uint32_t bi = i % 4;
        bytes[num_bytes - 1u - i] = static_cast<uint8_t>((words[wi] >> (bi * 8u)) & 0xFFu);
    }
    return bytes;
}

std::vector<uint8_t> make_instruction(bool vld,
                                      bool jump,
                                      uint8_t embed,
                                      uint16_t loops,
                                      uint8_t cond,
                                      uint8_t dly,
                                      uint16_t inc,
                                      uint8_t embed_end_bits,
                                      uint8_t inc_embed_bits,
                                      const std::vector<uint8_t>& code_bytes,
                                      uint32_t code_bits) {
    const uint64_t header_bits = serialize_header_bits(vld, jump, embed, loops, cond, dly, inc,
                                                       embed_end_bits, inc_embed_bits);
    const uint32_t header_bits_width = 1 + 1 + BITS_EMBED + BITS_EMBED_END + BITS_LOOPS +
                                       BITS_COND + BITS_DLY + BITS_INC + BITS_INC_EMBED;
    const uint32_t total_bits = header_bits_width + code_bits;
    const uint32_t total_bytes = (total_bits + 7u) / 8u;
    std::vector<uint8_t> result(total_bytes, 0);

    const uint32_t code_start = total_bytes - static_cast<uint32_t>(code_bytes.size());
    for (uint32_t i = 0; i < code_bytes.size(); ++i) {
        result[code_start + i] = code_bytes[i];
    }

    for (uint32_t bit = 0; bit < header_bits_width; ++bit) {
        const uint8_t v = static_cast<uint8_t>((header_bits >> bit) & 1u);
        if (!v) {
            continue;
        }
        const uint32_t global_bit = code_bits + bit;
        const uint32_t byte_from_lsb = global_bit / 8u;
        const uint32_t bit_in_byte = global_bit % 8u;
        const uint32_t idx = total_bytes - 1u - byte_from_lsb;
        result[idx] = static_cast<uint8_t>(result[idx] | (1u << bit_in_byte));
    }

    return result;
}

} // namespace detail

std::vector<uint8_t> make_iq_idle_instruction(uint32_t width_bits, uint16_t idle_cycles) {
    if (idle_cycles == 0) return {};
    constexpr uint32_t kHeaderBits =
        1u + 1u + detail::BITS_EMBED + detail::BITS_EMBED_END + detail::BITS_LOOPS +
        detail::BITS_COND + detail::BITS_DLY + detail::BITS_INC + detail::BITS_INC_EMBED;
    const uint32_t code_bits = width_bits - kHeaderBits;
    std::vector<uint8_t> zero_code((code_bits + 7u) / 8u, 0u);
    return detail::make_instruction(true, false, 0, static_cast<uint16_t>(idle_cycles - 1u), 0, 0,
                                    0, 0, 0, zero_code, code_bits);
}

void fill_ldbaxi_cru(const LsuLoadParams& params, iq::LdbAxiCru& cru) {
    cru.para_vld = params.para_ldbaxi_vld;
    cru.para_first_byte_strb = static_cast<uint8_t>(params.para_first_byte_strb & 0x0F);
    cru.para_last_byte_strb = static_cast<uint8_t>(params.para_last_byte_strb & 0x0F);
    cru.para_brst = static_cast<uint8_t>(params.para_brst & 0x0F);
    cru.para_gr_base_addr = params.para_gr_base_addr & 0xFFFFFFFFFFULL;
    cru.para_gr_intlv_addr = params.para_gr_intlv_addr & 0xFFFFFFFFFFULL;
    cru.para_inst_id = static_cast<uint8_t>(params.para_inst_id & 0x0F);
    cru.para_smc_id_en = static_cast<uint8_t>(params.para_smc_id_en & 0x1F);
    cru.para_tid = static_cast<iq::LdbAxiTid>(params.para_tid & 0x03);
}

void fill_ldb_cru(const LsuLoadParams& params, iq::LdbCru& cru) {
    cru.para_vld = params.para_ldb_vld;
    cru.para_ur_addr = static_cast<uint16_t>(params.para_ur_addr & 0x7FFF);
    cru.para_ur_wr_line_cnt = params.para_ur_wr_line_cnt;
    cru.para_line_byte_cnt = static_cast<uint16_t>(params.para_line_byte_cnt & 0x01FF);
    cru.para_line_stride = static_cast<uint8_t>(params.para_line_stride & 0x3F);
    cru.para_const = static_cast<uint8_t>(params.para_const & 0x3F);
    cru.para_pre_fill = params.para_pre_fill;
    cru.para_post_fill = params.para_post_fill;
    cru.para_inst_id = static_cast<uint8_t>(params.para_inst_id & 0x0F);
    cru.para_smc_id_en = static_cast<uint8_t>(params.para_smc_id_en & 0x1F);
}

void fill_stb_cru(const LsuStoreParams& params, iq::StbCru& cru) {
    cru.para_vld = params.para_stb_vld;
    cru.para_ur_addr = static_cast<uint16_t>(params.para_ur_addr & 0x7FFFu);
    cru.para_ur_rd_line_cnt = params.para_ur_rd_line_cnt;
    cru.para_line_byte_cnt = static_cast<uint16_t>(params.para_line_byte_cnt & 0x1FFu);
    cru.para_line_stride = static_cast<uint8_t>(params.para_line_stride & 0x3Fu);
    cru.para_inst_id = static_cast<uint8_t>(params.para_inst_id & 0xFu);
    cru.para_smc_id_en = static_cast<uint8_t>(params.para_smc_id_en & 0x1Fu);
    cru.para_tid = static_cast<iq::StbTid>(params.para_tid & 0x3u);
}

void fill_stbaxi_cru(const LsuStoreParams& params, iq::StbAxiCru& cru) {
    cru.para_vld = params.para_stbaxi_vld;
    cru.para_first_byte_strb = static_cast<uint8_t>(params.para_first_byte_strb & 0xFu);
    cru.para_last_byte_strb = static_cast<uint8_t>(params.para_last_byte_strb & 0xFu);
    cru.para_brst = static_cast<uint8_t>(params.para_brst & 0xFu);
    cru.para_gr_base_addr = params.para_gr_base_addr & 0xFFFFFFFFFFULL;
    cru.para_gr_intlv_addr = params.para_gr_intlv_addr & 0xFFFFFFFFFFULL;
    cru.para_inst_id = static_cast<uint8_t>(params.para_inst_id & 0xFu);
    cru.para_smc_id_en = static_cast<uint8_t>(params.para_smc_id_en & 0x1Fu);
}

std::vector<IqInstructionFile> emit_load_iq_files(const LsuLoadParams& params) {
    std::vector<IqInstructionFile> files;
    files.reserve(2);

    iq::LdbAxiCru ldbaxi_cru{};
    iq::LdbCru ldb_cru{};
    fill_ldbaxi_cru(params, ldbaxi_cru);
    fill_ldb_cru(params, ldb_cru);

    {
        uint32_t words[4] = {0, 0, 0, 0};
        iq::LdbIq::pack_cru_ldbaxi(ldbaxi_cru, words);
        const auto code_bytes = detail::serialize_code_words_lsb_first(words, 104);
        IqInstructionFile file;
        file.para_iq_id = iq::IQ_ID_LDBAXI;
        file.para_iq_name = iq::IQ_NAME_LDBAXI;
        file.para_width_bits = 50 + 104;
        file.para_data = detail::make_instruction(params.para_ldbaxi_vld,
                                          params.para_ldbaxi_jump,
                                          static_cast<uint8_t>(params.para_ldbaxi_embed & detail::MASK_EMBED),
                                          static_cast<uint16_t>(params.para_ldbaxi_loops & detail::MASK_LOOPS),
                                          static_cast<uint8_t>(params.para_ldbaxi_cond & detail::MASK_COND),
                                          static_cast<uint8_t>(params.para_ldbaxi_dly & detail::MASK_DLY),
                                          static_cast<uint16_t>(params.para_ldbaxi_inc & detail::MASK_INC),
                                          static_cast<uint8_t>(params.para_ldbaxi_embed_end & detail::MASK_EMBED_END),
                                          static_cast<uint8_t>(params.para_ldbaxi_inc_embed & detail::MASK_INC_EMBED),
                                          code_bytes,
                                          104);
        files.push_back(std::move(file));
    }

    {
        uint32_t words[2] = {0, 0};
        iq::LdbIq::pack_cru_ldb(ldb_cru, words);
        const auto code_bytes = detail::serialize_code_words_lsb_first(words, 56);
        IqInstructionFile file;
        file.para_iq_id = iq::IQ_ID_LDB;
        file.para_iq_name = iq::IQ_NAME_LDB;
        file.para_width_bits = 50 + 56;
        file.para_data = detail::make_instruction(params.para_ldb_vld,
                                          params.para_ldb_jump,
                                          static_cast<uint8_t>(params.para_ldb_embed & detail::MASK_EMBED),
                                          static_cast<uint16_t>(params.para_ldb_loops & detail::MASK_LOOPS),
                                          static_cast<uint8_t>(params.para_ldb_cond & detail::MASK_COND),
                                          static_cast<uint8_t>(params.para_ldb_dly & detail::MASK_DLY),
                                          static_cast<uint16_t>(params.para_ldb_inc & detail::MASK_INC),
                                          static_cast<uint8_t>(params.para_ldb_embed_end & detail::MASK_EMBED_END),
                                          static_cast<uint8_t>(params.para_ldb_inc_embed & detail::MASK_INC_EMBED),
                                          code_bytes,
                                          56);
        files.push_back(std::move(file));
    }

    return files;
}

std::vector<IqInstructionFile> emit_store_iq_files(const LsuStoreParams& params) {
    std::vector<IqInstructionFile> files;
    files.reserve(2);

    iq::StbCru stb_cru{};
    iq::StbAxiCru stbaxi_cru{};
    fill_stb_cru(params, stb_cru);
    fill_stbaxi_cru(params, stbaxi_cru);

    {
        uint32_t words[2] = {0, 0};
        iq::StbIq::pack_cru_stb(stb_cru, words);
        const auto code_bytes = detail::serialize_code_words_lsb_first(words, 50);
        IqInstructionFile file;
        file.para_iq_id = iq::IQ_ID_STB;
        file.para_iq_name = iq::IQ_NAME_STB;
        file.para_width_bits = 50 + 50;
        file.para_data = detail::make_instruction(params.para_stb_vld,
                                          params.para_stb_jump,
                                          static_cast<uint8_t>(params.para_stb_embed & detail::MASK_EMBED),
                                          static_cast<uint16_t>(params.para_stb_loops & detail::MASK_LOOPS),
                                          static_cast<uint8_t>(params.para_stb_cond & detail::MASK_COND),
                                          static_cast<uint8_t>(params.para_stb_dly & detail::MASK_DLY),
                                          static_cast<uint16_t>(params.para_stb_inc & detail::MASK_INC),
                                          static_cast<uint8_t>(params.para_stb_embed_end & detail::MASK_EMBED_END),
                                          static_cast<uint8_t>(params.para_stb_inc_embed & detail::MASK_INC_EMBED),
                                          code_bytes,
                                          50);
        files.push_back(std::move(file));
    }

    {
        uint32_t words[4] = {0, 0, 0, 0};
        iq::StbIq::pack_cru_stbaxi(stbaxi_cru, words);
        const auto code_bytes = detail::serialize_code_words_lsb_first(words, 102);
        IqInstructionFile file;
        file.para_iq_id = iq::IQ_ID_STBAXI;
        file.para_iq_name = iq::IQ_NAME_STBAXI;
        file.para_width_bits = 50 + 102;
        file.para_data = detail::make_instruction(params.para_stbaxi_vld,
                                          params.para_stbaxi_jump,
                                          static_cast<uint8_t>(params.para_stbaxi_embed & detail::MASK_EMBED),
                                          static_cast<uint16_t>(params.para_stbaxi_loops & detail::MASK_LOOPS),
                                          static_cast<uint8_t>(params.para_stbaxi_cond & detail::MASK_COND),
                                          static_cast<uint8_t>(params.para_stbaxi_dly & detail::MASK_DLY),
                                          static_cast<uint16_t>(params.para_stbaxi_inc & detail::MASK_INC),
                                          static_cast<uint8_t>(params.para_stbaxi_embed_end & detail::MASK_EMBED_END),
                                          static_cast<uint8_t>(params.para_stbaxi_inc_embed & detail::MASK_INC_EMBED),
                                          code_bytes,
                                          102);
        files.push_back(std::move(file));
    }

    return files;
}

} // namespace op
} // namespace aiacc
