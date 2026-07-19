#include "lsu_op_base.h"
#include "../iq/iq_id_defs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace aiacc {
namespace lsu {

namespace {

constexpr uint32_t kLsuHeaderBits = 50;
constexpr uint8_t kMaxDlyField = 7;
constexpr uint8_t kNoEmbed = 7;
constexpr uint16_t kBulkSplitWords = 128;

bool allow_1beat_debug() {
    const char* value = std::getenv("LSU_ALLOW_1BEAT");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

uint64_t make_lsu_delay_header(uint8_t dly) {
    uint64_t bits = 0;
    uint32_t pos = 0;
    pos += 6;   // inc_embed
    pos += 13;  // inc
    bits |= (static_cast<uint64_t>(dly) & 0x7u) << pos;
    pos += 3;
    pos += 7;   // cond
    pos += 10;  // loops
    pos += 6;   // embed_end
    bits |= (static_cast<uint64_t>(kNoEmbed) & 0x7u) << pos;
    pos += 3;
    pos += 1;   // jump
    bits |= 1ull << pos;  // vld
    return bits;
}

std::vector<uint8_t> make_lsu_delay_nop(uint32_t width_bits, uint8_t dly) {
    const uint32_t code_bits = (width_bits > kLsuHeaderBits) ? (width_bits - kLsuHeaderBits) : 0;
    const uint32_t total_bytes = (width_bits + 7u) / 8u;
    std::vector<uint8_t> bytes(total_bytes, 0);
    const uint64_t header = make_lsu_delay_header(dly);
    for (uint32_t bit = 0; bit < kLsuHeaderBits; ++bit) {
        if (((header >> bit) & 1u) == 0) continue;
        const uint32_t global_bit = code_bits + bit;
        const uint32_t byte_from_lsb = global_bit / 8u;
        const uint32_t bit_in_byte = global_bit % 8u;
        const uint32_t idx = total_bytes - 1u - byte_from_lsb;
        bytes[idx] = static_cast<uint8_t>(bytes[idx] | (1u << bit_in_byte));
    }
    return bytes;
}

} // namespace

uint16_t LsuOpBase::compose_ur_addr(uint8_t bank, uint16_t word_addr) {
    return static_cast<uint16_t>(((bank & 0x7u) << 12) | ((word_addr & 0xFFu) << 4));
}

uint16_t LsuOpBase::calc_ldb_loops(uint16_t word_count) const {
    return (word_count > 0) ? static_cast<uint16_t>(word_count - 1) : 0;
}

uint8_t LsuOpBase::calc_ldbaxi_loops(uint16_t word_count) const {
    uint32_t bursts = (word_count + 15) / 16;
    return static_cast<uint8_t>((bursts > 0) ? (bursts - 1) : 0);
}

std::vector<LsuOpBase::BurstSeg> LsuOpBase::bulk_burst_segments(uint16_t N) {
    std::vector<BurstSeg> segs;
    if (N == 0) return segs;
    if (N == 1) {
        // 4.21 RTL is not reliable with brst=0. Move one real word plus one
        // padding word; the padding lands in the aligned gap and is ignored.
        segs.push_back({2, 1, 0});
        return segs;
    }
    uint16_t full = static_cast<uint16_t>(N / 16);
    uint16_t rem  = static_cast<uint16_t>(N % 16);
    if (full == 0) {                       // N in [1,15]：单段 N 拍
        segs.push_back({N, 1, 0});
        return segs;
    }
    uint64_t off = 0;
    if (rem == 1) {
        // 末尾 17 拍拆成 9+8，避免出现 brst=0（单拍 burst 会让 LDBAXI 卡住）
        if (full - 1 > 0) {
            segs.push_back({16, static_cast<uint16_t>(full - 1), 0});
            off = static_cast<uint64_t>(full - 1) * 256u;
        }
        segs.push_back({9, 1, off}); off += 9u * 16u;
        segs.push_back({8, 1, off});
    } else {
        segs.push_back({16, full, 0});
        off = static_cast<uint64_t>(full) * 256u;
        if (rem >= 2) segs.push_back({rem, 1, off});
    }
    return segs;
}

std::vector<LsuOpBase::BurstSeg> LsuOpBase::bulk_burst_segments_at_base(uint64_t byte_base, uint16_t N) {
    std::vector<BurstSeg> segs;
    uint16_t remaining = N;
    uint64_t off = 0;
    while (remaining > 0) {
        const uint64_t addr = byte_base + off;
        uint16_t page_words = static_cast<uint16_t>((0x1000u - (addr & 0xFFFu)) / 16u);
        if (page_words == 0) page_words = 1;
        uint16_t beats = std::min<uint16_t>(remaining, 16);
        beats = std::min<uint16_t>(beats, page_words);
        segs.push_back({beats, 1, off});
        remaining = static_cast<uint16_t>(remaining - beats);
        off += static_cast<uint64_t>(beats) * 16u;
    }
    return segs;
}

uint16_t LsuOpBase::bulk_total_beats(uint16_t word_count) {
    uint32_t total = 0;
    for (const auto& seg : bulk_burst_segments(word_count)) {
        total += static_cast<uint32_t>(seg.beats) * seg.num;
    }
    return static_cast<uint16_t>(std::min<uint32_t>(total, 256u));
}

void LsuOpBase::prepend_start_delay(op::IqInstructionFile& file, uint32_t delay_cycles) {
    if (delay_cycles == 0 || file.para_width_bits <= kLsuHeaderBits) return;

    std::vector<uint8_t> prefix;
    while (delay_cycles > kMaxDlyField) {
        auto nop = make_lsu_delay_nop(file.para_width_bits, kMaxDlyField);
        prefix.insert(prefix.end(), nop.begin(), nop.end());
        delay_cycles -= 8;
    }
    if (delay_cycles > 0) {
        auto nop = make_lsu_delay_nop(file.para_width_bits, static_cast<uint8_t>(delay_cycles - 1));
        prefix.insert(prefix.end(), nop.begin(), nop.end());
    }
    if (!prefix.empty()) {
        prefix.insert(prefix.end(), file.para_data.begin(), file.para_data.end());
        file.para_data.swap(prefix);
    }
}

static void append_bytes(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

struct WordSpan {
    uint16_t word_off;
    uint16_t words;
};

static std::vector<WordSpan> bulk_word_spans(uint16_t word_count) {
    if (word_count == 0) return {};
    if (word_count > 256) {
        throw std::runtime_error("bulk word_count must be <=256 per UR bank depth");
    }
    if (word_count == 256) {
        return {{0, kBulkSplitWords}, {kBulkSplitWords, kBulkSplitWords}};
    }
    return {{0, word_count}};
}

static std::vector<op::IqInstructionFile> make_store_delay_pair(uint8_t dly, uint8_t cond, uint64_t scratch_base,
                                                                uint64_t intlv_addr, uint8_t smc_id_en) {
    op::LsuStoreParams p{};
    p.para_gr_base_addr = scratch_base;
    p.para_gr_intlv_addr = intlv_addr;
    p.para_brst = 1;                   // 2 beats; avoids single-beat STBAXI corner cases.
    p.para_first_byte_strb = 0;
    p.para_last_byte_strb = 0;
    p.para_ur_addr = static_cast<uint16_t>(4u << 12); // bank4 scratch, word 0
    p.para_ur_rd_line_cnt = 2;
    p.para_line_byte_cnt = 0x0F;
    p.para_line_stride = 1;
    p.para_tid = 2;
    p.para_smc_id_en = smc_id_en;
    p.para_stb_loops = 0;
    p.para_stbaxi_loops = 0;
    p.para_stb_inc = 0x0010;
    p.para_stbaxi_inc = 0x0100;
    p.para_stb_dly = dly;
    p.para_stbaxi_dly = dly;
    p.para_stb_cond = cond;
    p.para_stbaxi_cond = cond;
    p.para_stb_vld = true;
    p.para_stbaxi_vld = true;
    return op::emit_store_iq_files(p);
}

static void apply_store_start_delay(std::vector<op::IqInstructionFile>& files, uint32_t delay_cycles, uint8_t start_cond,
                                    uint64_t scratch_base, uint64_t intlv_addr, uint8_t smc_id_en) {
    if (delay_cycles == 0) return;

    std::vector<uint8_t> stb_prefix;
    std::vector<uint8_t> stbaxi_prefix;
    uint32_t remaining = delay_cycles;
    while (remaining > 0) {
        const uint8_t dly = (remaining > 8) ? kMaxDlyField : static_cast<uint8_t>(remaining - 1);
        for (const auto& f : make_store_delay_pair(dly, start_cond, scratch_base, intlv_addr, smc_id_en)) {
            if (f.para_iq_id == iq::IQ_ID_STB) append_bytes(stb_prefix, f.para_data);
            if (f.para_iq_id == iq::IQ_ID_STBAXI) append_bytes(stbaxi_prefix, f.para_data);
        }
        start_cond = 0;
        remaining = (remaining > 8) ? (remaining - 8) : 0;
    }

    bool delayed_stb = false;
    bool delayed_stbaxi = false;
    for (auto& f : files) {
        if (f.para_iq_id == iq::IQ_ID_STB && !delayed_stb) {
            stb_prefix.insert(stb_prefix.end(), f.para_data.begin(), f.para_data.end());
            f.para_data.swap(stb_prefix);
            delayed_stb = true;
        } else if (f.para_iq_id == iq::IQ_ID_STBAXI && !delayed_stbaxi) {
            stbaxi_prefix.insert(stbaxi_prefix.end(), f.para_data.begin(), f.para_data.end());
            f.para_data.swap(stbaxi_prefix);
            delayed_stbaxi = true;
        }
    }
}

uint16_t LsuOpBase::largest_burst_beats(uint16_t word_count) {
    // 取 <=16 且能整除 word_count 的最大拍数，使 (拍数 × burst 数) == word_count，AXI 不多发
    if (word_count == 0) return 1;
    for (uint16_t b = (word_count < 16 ? word_count : 16); b >= 1; --b) {
        if (word_count % b == 0) return b;
    }
    return 1;
}

uint32_t LsuOpBase::total_input_words() const {
    uint32_t total = 0;
    for (auto& [name, port] : desc_.inputs) {
        total += port.word_count;
    }
    return total;
}

uint64_t LsuOpBase::input_gr_base(const std::string& name) const {
    if (desc_.full_ddr_mode) {
        auto it = desc_.full_input_byte_base.find(name);
        if (it != desc_.full_input_byte_base.end()) {
            uint64_t base = it->second;
            if (name != "alpha") {
                base += desc_.full_word_offset * 16u;
            }
            return base;
        }
    }

    // bulk 模式：HBM 数据连续存放，基址按 word_count*16 精确连续；
    // loops 模式：沿用 (word_count+1)*16 约定（与 loops 下 AXI 寻址对齐）。
    uint64_t offset = 0;
    for (auto& [n, port] : desc_.inputs) {
        if (n == name) return offset;
        offset += seg_stride_bytes(port.word_count);
    }
    return offset;
}

uint64_t LsuOpBase::store_gr_base() const {
    if (desc_.full_ddr_mode) {
        return desc_.full_output_byte_base + desc_.full_word_offset * 16u;
    }

    uint64_t offset = 0;
    for (auto& [name, port] : desc_.inputs) {
        offset += seg_stride_bytes(port.word_count);
    }
    return offset;
}

uint64_t LsuOpBase::seg_stride_bytes(uint16_t word_count) const {
    if (desc_.bulk_mode) {
        // bulk：每段按 4KB(256 行) 对齐，避免大 burst 跨 4KB 边界（AXI 非法跨界会回卷）
        uint32_t lines = ((static_cast<uint32_t>(word_count) + 255u) / 256u) * 256u;
        return static_cast<uint64_t>(lines) * 16u;
    }
    // loops：沿用 (word_count+1)*16 约定
    return static_cast<uint64_t>(word_count + 1u) * 16u;
}

op::LsuLoadParams LsuOpBase::build_load_params(uint8_t bank, uint16_t word_offset,
                                                uint16_t word_count, uint64_t gr_base) const {
    op::LsuLoadParams p{};
    p.para_ur_addr = compose_ur_addr(bank, word_offset);
    p.para_line_byte_cnt = 0x0F;            // 16 字节/行
    p.para_line_stride = 1;
    p.para_smc_id_en = desc_.smc_id_en;
    p.para_gr_base_addr = gr_base;
    p.para_gr_intlv_addr = desc_.gr_intlv_addr;
    p.para_ldb_vld = true;
    p.para_ldbaxi_vld = true;

    if (desc_.bulk_mode) {
        // LDB：一条指令写 word_count 行（ur_wr_line_cnt），不用外壳 loops
        const uint16_t total_beats = desc_.full_ddr_mode ? word_count : bulk_total_beats(word_count);
        p.para_ur_wr_line_cnt = total_beats;
        p.para_ldb_loops = 0;
        p.para_ldb_inc = 0x0010;
        // LDBAXI 主体：16 拍/burst（brst=15），共 floor(N/16) 个 burst，地址每次 +0x100。
        // 余数 N%16 由 generate_load_iq_files 追加一条 LDBAXI 处理（见那里）。
        uint16_t full_bursts = static_cast<uint16_t>(word_count / 16);
        uint16_t rem = static_cast<uint16_t>(word_count % 16);
        if (full_bursts >= 1) {
            p.para_brst = 0x0F;                                   // 16 拍
            p.para_ldbaxi_loops = static_cast<uint16_t>(full_bursts - 1);
            p.para_ldbaxi_inc = 0x0100;                           // 每 burst +256B(16 行)
        } else {
            // N<16：主体即余数，一条 N 拍 burst
            p.para_brst = static_cast<uint8_t>((rem - 1) & 0x0F);
            p.para_ldbaxi_loops = 0;
            p.para_ldbaxi_inc = 0x0100;
        }
    } else {
        // loops 模式：每条 LDB 写 1 行，外壳 loops 重复 word_count 次
        p.para_ur_wr_line_cnt = 1;
        p.para_ldb_loops = calc_ldb_loops(word_count);
        p.para_ldbaxi_loops = calc_ldbaxi_loops(word_count);
        p.para_brst = 0x0F;
        p.para_ldb_inc = 0x0010;
        p.para_ldbaxi_inc = 0x0100;
    }
    return p;
}

op::LsuStoreParams LsuOpBase::build_store_params(uint8_t bank, uint16_t word_offset,
                                                  uint16_t word_count, uint64_t gr_base) const {
    op::LsuStoreParams p{};
    p.para_ur_addr = compose_ur_addr(bank, word_offset);
    p.para_line_byte_cnt = 0x0F;
    p.para_line_stride = 1;
    p.para_smc_id_en = desc_.smc_id_en;
    p.para_gr_base_addr = gr_base;
    p.para_gr_intlv_addr = desc_.gr_intlv_addr;
    p.para_stb_vld = true;
    p.para_stbaxi_vld = true;
    p.para_tid = 2;

    if (desc_.bulk_mode) {
        const uint16_t total_beats = desc_.full_ddr_mode ? word_count : bulk_total_beats(word_count);
        p.para_ur_rd_line_cnt = total_beats;
        p.para_stb_loops = 0;
        p.para_stb_inc = 0x0010;
        uint16_t full_bursts = static_cast<uint16_t>(word_count / 16);
        uint16_t rem = static_cast<uint16_t>(word_count % 16);
        if (full_bursts >= 1) {
            p.para_brst = 0x0F;
            p.para_stbaxi_loops = static_cast<uint16_t>(full_bursts - 1);
            p.para_stbaxi_inc = 0x0100;
        } else {
            p.para_brst = static_cast<uint8_t>((rem - 1) & 0x0F);
            p.para_stbaxi_loops = 0;
            p.para_stbaxi_inc = 0x0100;
        }
    } else {
        p.para_ur_rd_line_cnt = 1;
        p.para_stb_loops = calc_ldb_loops(word_count);
        p.para_stbaxi_loops = calc_ldbaxi_loops(word_count);
        p.para_brst = 0x0F;
        p.para_stb_inc = 0x0010;
        p.para_stbaxi_inc = 0x0100;
    }
    return p;
}

std::vector<op::IqInstructionFile> LsuOpBase::generate_load_iq_files() const {
    std::vector<op::IqInstructionFile> all;
    for (auto& [name, port] : desc_.inputs) {
        uint64_t gr_base = input_gr_base(name);

        if (!desc_.bulk_mode) {
            auto p = build_load_params(port.bank, port.word_offset, port.word_count, gr_base);
            auto files = op::emit_load_iq_files(p);
            for (auto& f : files) all.push_back(std::move(f));
            continue;
        }
        // bulk：word_count=256 时拆成 128+128，两条合法 LDB 装满一个 UR bank。
        const bool allow_load_padding = desc_.full_ddr_mode && name == "alpha";
        for (const auto& span : bulk_word_spans(port.word_count)) {
            const uint64_t span_byte_off = static_cast<uint64_t>(span.word_off) * 16u;
            auto p = build_load_params(port.bank,
                                       static_cast<uint16_t>(port.word_offset + span.word_off),
                                       span.words,
                                       gr_base + span_byte_off);
            if (allow_load_padding) {
                p.para_ur_wr_line_cnt = bulk_total_beats(span.words);
            }
            {
                auto lf = op::emit_load_iq_files(p);
                for (auto& f : lf) if (f.para_iq_id == iq::IQ_ID_LDB) all.push_back(std::move(f));
            }
            const auto segs = (desc_.full_ddr_mode && !allow_load_padding)
                ? bulk_burst_segments_at_base(gr_base + span_byte_off, span.words)
                : bulk_burst_segments(span.words);
            for (auto& seg : segs) {
                if (desc_.full_ddr_mode && !allow_load_padding && seg.beats == 1 && !allow_1beat_debug()) {
                    throw std::runtime_error(
                        "full-DDR load chunk requires an unsupported 1-beat LDBAXI burst; "
                        "use run_llama_sub_chunks.sh page-safe overlap or choose a different chunk");
                }
                auto ps = p;
                ps.para_brst = static_cast<uint8_t>((seg.beats - 1) & 0x0F);
                ps.para_ldbaxi_loops = static_cast<uint16_t>(seg.num - 1);
                ps.para_ldbaxi_inc = 0x0100;                 // 16 拍主体段每 burst +256B
                ps.para_gr_base_addr = gr_base + span_byte_off + seg.byte_off;
                auto sf = op::emit_load_iq_files(ps);
                for (auto& f : sf) if (f.para_iq_id == iq::IQ_ID_LDBAXI) all.push_back(std::move(f));
            }
        }
    }
    return all;
}

std::vector<op::IqInstructionFile> LsuOpBase::generate_store_iq_files() const {
    std::vector<op::IqInstructionFile> all;
    uint64_t gr_base = store_gr_base();

    if (!desc_.bulk_mode) {
        auto p = build_store_params(desc_.output.bank, desc_.output.word_offset,
                                    desc_.output.word_count, gr_base);
        auto files = op::emit_store_iq_files(p);
        apply_store_start_delay(files, desc_.store_start_delay, desc_.store_start_cond,
                                store_gr_base() + seg_stride_bytes(desc_.output.word_count),
                                desc_.gr_intlv_addr, desc_.smc_id_en);
        return files;
    }
    // bulk：word_count=256 时拆成 128+128，两条合法 STB 把一个 UR bank 写回 DDR。
    for (const auto& span : bulk_word_spans(desc_.output.word_count)) {
        const uint64_t span_byte_off = static_cast<uint64_t>(span.word_off) * 16u;
        auto p = build_store_params(desc_.output.bank,
                                    static_cast<uint16_t>(desc_.output.word_offset + span.word_off),
                                    span.words,
                                    gr_base + span_byte_off);
        {
            auto sf = op::emit_store_iq_files(p);
            for (auto& f : sf) if (f.para_iq_id == iq::IQ_ID_STB) all.push_back(std::move(f));
        }
        const auto segs = desc_.full_ddr_mode
            ? bulk_burst_segments_at_base(gr_base + span_byte_off, span.words)
            : bulk_burst_segments(span.words);
        for (auto& seg : segs) {
            if (desc_.full_ddr_mode && seg.beats == 1 && !allow_1beat_debug()) {
                throw std::runtime_error(
                    "full-DDR store chunk requires an unsupported 1-beat STBAXI burst; "
                    "use run_llama_sub_chunks.sh page-safe overlap or choose a different chunk");
            }
            auto ps = p;
            ps.para_brst = static_cast<uint8_t>((seg.beats - 1) & 0x0F);
            ps.para_stbaxi_loops = static_cast<uint16_t>(seg.num - 1);
            ps.para_stbaxi_inc = 0x0100;
            ps.para_gr_base_addr = gr_base + span_byte_off + seg.byte_off;
            auto sf = op::emit_store_iq_files(ps);
            for (auto& f : sf) if (f.para_iq_id == iq::IQ_ID_STBAXI) all.push_back(std::move(f));
        }
    }
    apply_store_start_delay(all, desc_.store_start_delay, desc_.store_start_cond,
                            store_gr_base() + seg_stride_bytes(desc_.output.word_count),
                            desc_.gr_intlv_addr, desc_.smc_id_en);
    return all;
}

void LsuOpBase::generate_hbm_data(uint8_t smc_id, uint32_t seed,
                                   std::vector<uint8_t>& hbm_bytes) const {
    std::mt19937 rng(seed + smc_id * 1000);
    // 跨数量级随机：尾数 [1,2) × 10^E（E∈[-20,20]）× 随机符号，仅有限值。
    // 范围约 ±1e-20 ~ ±2e20，覆盖正负与极大/极小，避免直接生成 inf/NaN/非规格化数。
    std::uniform_real_distribution<float> mant(1.0f, 2.0f);
    std::uniform_int_distribution<int> expo(-20, 20);
    std::uniform_int_distribution<int> sgn(0, 1);
    auto gen_val = [&]() -> float {
        float v = mant(rng) * std::pow(10.0f, static_cast<float>(expo(rng)));
        return sgn(rng) ? -v : v;
    };

    // Contiguous: all input segments back-to-back, no gap in HBM data
    // gr_base for segment N = sum of (prev_word_count + 1) * 16
    // This +1 is an addressing offset, NOT a gap in data
    auto write_fp32 = [&](uint64_t byte_off) {
        float val = gen_val();
        uint32_t bits;
        std::memcpy(&bits, &val, 4);
        hbm_bytes[byte_off + 0] = static_cast<uint8_t>(bits & 0xFF);
        hbm_bytes[byte_off + 1] = static_cast<uint8_t>((bits >> 8) & 0xFF);
        hbm_bytes[byte_off + 2] = static_cast<uint8_t>((bits >> 16) & 0xFF);
        hbm_bytes[byte_off + 3] = static_cast<uint8_t>((bits >> 24) & 0xFF);
    };

    if (!desc_.bulk_mode) {
        // loops：所有输入连续存放
        uint32_t total_words = 0;
        for (auto& [name, port] : desc_.inputs) total_words += port.word_count;
        hbm_bytes.resize(total_words * 16, 0);
        for (uint32_t i = 0; i < total_words * 4; ++i) write_fp32(i * 4);
    } else {
        // bulk：每段放在 input_gr_base(name) 对齐基址，段间空隙补 0
        uint64_t total = 0;
        for (auto& [name, port] : desc_.inputs)
            total = std::max(total, input_gr_base(name) + static_cast<uint64_t>(port.word_count) * 16);
        hbm_bytes.resize(total, 0);
        for (auto& [name, port] : desc_.inputs) {
            uint64_t base = input_gr_base(name);
            for (uint32_t i = 0; i < static_cast<uint32_t>(port.word_count) * 4; ++i)
                write_fp32(base + static_cast<uint64_t>(i) * 4);
        }
    }
}

static void mkdir_p(const std::string& path) {
    (void)system((std::string("mkdir -p \"") + path + "\"").c_str());
}

static void write_hex_file(const std::string& path, const std::vector<uint8_t>& data,
                           uint32_t line_width_bytes) {
    std::ofstream ofs(path);
    if (!ofs) return;
    uint32_t lines = static_cast<uint32_t>(data.size()) / line_width_bytes;
    for (uint32_t l = 0; l < lines; ++l) {
        for (int b = static_cast<int>(line_width_bytes) - 1; b >= 0; --b) {
            uint8_t byte = data[l * line_width_bytes + b];
            ofs << std::hex << std::setfill('0') << std::setw(2)
                << static_cast<unsigned>(byte);
        }
        ofs << "\n";
    }
}

static void write_zero_bank(const std::string& path, uint32_t lines) {
    std::ofstream ofs(path);
    if (!ofs) return;
    std::string zero_line(32, '0');
    for (uint32_t i = 0; i < lines; ++i) {
        ofs << zero_line << "\n";
    }
}

static std::string iq_filename_from_name(uint32_t /*id*/, const std::string& raw_name) {
    if (raw_name == "ldbaxi") return "ldb_axi";
    if (raw_name == "stbaxi") return "stb_axi";
    if (raw_name == "ur_rdata_0") return "ur-r0";
    if (raw_name == "ur_rdata_1") return "ur-r1";
    if (raw_name == "ur_rdata_2") return "ur-r2";
    if (raw_name == "ur_rdata_3") return "ur-r3";
    if (raw_name == "ur_wdata_0") return "ur-w0";
    if (raw_name == "ur_wdata_1") return "ur-w1";
    if (raw_name == "ur_wdata_2") return "ur-w2";
    if (raw_name == "ur_wdata_3") return "ur-w3";
    return raw_name;
}

// 把一组 IQ 文件按队列名合并后，写到指定目录下的 iq_<name>.hex（每条指令一行）。
// v0.4.20 multi_inst_stage：指令按 stage 分目录放在 case_root/smi_iq_inst/stage_<k>/，
// 是 smc_* 的同级（case 级），不再放进 smc_0/。
static void write_iq_files_to_dir(const std::string& dir,
                                  const std::vector<op::IqInstructionFile>& files) {
    struct IqFileEntry {
        std::vector<uint8_t> data;
        uint32_t width_bits{0};
    };
    std::map<std::string, IqFileEntry> file_map;
    for (auto& f : files) {
        std::string fname = iq_filename_from_name(f.para_iq_id, f.para_iq_name);
        auto& entry = file_map[fname];
        entry.data.insert(entry.data.end(), f.para_data.begin(), f.para_data.end());
        if (entry.width_bits == 0) entry.width_bits = f.para_width_bits;
    }
    for (auto& [fname, entry] : file_map) {
        std::string path = dir + "/iq_" + fname + ".hex";
        std::ofstream ofs(path);
        if (!ofs) continue;
        uint32_t inst_bytes = (entry.width_bits + 7) / 8;
        if (inst_bytes == 0) inst_bytes = static_cast<uint32_t>(entry.data.size());
        uint32_t num_insts = static_cast<uint32_t>(entry.data.size()) / inst_bytes;
        for (uint32_t n = 0; n < num_insts; ++n) {
            for (uint32_t b = 0; b < inst_bytes; ++b) {
                ofs << std::hex << std::setfill('0') << std::setw(2)
                    << static_cast<unsigned>(entry.data[n * inst_bytes + b]);
            }
            ofs << "\n";
        }
    }
}

bool LsuOpBase::assemble_full_case() const {
    const std::string case_root = cfg_.case_root;
    mkdir_p(case_root);

    auto load_iqs = generate_load_iq_files();
    auto compute_iqs = generate_compute_iq_files();
    auto store_iqs = generate_store_iq_files();
    std::vector<op::IqInstructionFile> all_iqs;
    all_iqs.insert(all_iqs.end(), load_iqs.begin(), load_iqs.end());
    all_iqs.insert(all_iqs.end(), compute_iqs.begin(), compute_iqs.end());
    all_iqs.insert(all_iqs.end(), store_iqs.begin(), store_iqs.end());

    // ===== 4.19 三相触发版式：load+compute+store 指令合并写进 smc_0/iq_*.hex =====
    // （v3 的原生 bulk 发码不变，仅组装版式回退到 4.19 verification_v2，
    //  供 run_sim_aiacc_top_f.sh 消费；不再用 v0.4.20 的 smi_iq_inst/stage_* 分目录。）
    write_iq_files_to_dir(case_root, all_iqs);

    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data");
        mkdir_p(smc_dir + "/output_bank");

        for (int b = 0; b < 8; ++b) {
            write_zero_bank(smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex", 256);
        }

        if (smc == 0) {
            struct IqFileEntry { std::vector<uint8_t> data; uint32_t width_bits{0}; };
            std::map<std::string, IqFileEntry> file_map;
            auto collect_iq = [&](const std::vector<op::IqInstructionFile>& files) {
                for (auto& f : files) {
                    std::string fname = iq_filename_from_name(f.para_iq_id, f.para_iq_name);
                    auto& entry = file_map[fname];
                    entry.data.insert(entry.data.end(), f.para_data.begin(), f.para_data.end());
                    if (entry.width_bits == 0) entry.width_bits = f.para_width_bits;
                }
            };
            collect_iq(load_iqs);
            collect_iq(compute_iqs);
            collect_iq(store_iqs);

            for (auto& [fname, entry] : file_map) {
                std::string path = smc_dir + "/iq_" + fname + ".hex";
                std::ofstream ofs(path);
                if (!ofs) continue;
                uint32_t inst_bytes = (entry.width_bits + 7) / 8;
                if (inst_bytes == 0) inst_bytes = static_cast<uint32_t>(entry.data.size());
                uint32_t num_insts = static_cast<uint32_t>(entry.data.size()) / inst_bytes;
                for (uint32_t n = 0; n < num_insts; ++n) {
                    for (uint32_t b = 0; b < inst_bytes; ++b)
                        ofs << std::hex << std::setfill('0') << std::setw(2)
                            << static_cast<unsigned>(entry.data[n * inst_bytes + b]);
                    ofs << "\n";
                }
            }
        }

        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);

        std::string hbm_path = cfg_.hbm_init_dir + "/memory_init" + std::to_string(smc + 1) + ".hex";
        write_hex_file(hbm_path, hbm_data, 16);

        // 按与 HBM 写入相同的布局提取各输入段（loops=连续 / bulk=对齐基址）
        std::map<std::string, std::vector<uint8_t>> input_datas;
        if (!desc_.bulk_mode) {
            uint32_t byte_offset = 0;
            for (auto& [name, port] : desc_.inputs) {
                uint32_t data_bytes = port.word_count * 16;
                input_datas[name] = std::vector<uint8_t>(
                    hbm_data.begin() + byte_offset,
                    hbm_data.begin() + byte_offset + data_bytes);
                byte_offset += data_bytes;
            }
        } else {
            for (auto& [name, port] : desc_.inputs) {
                uint64_t base = input_gr_base(name);
                uint32_t data_bytes = port.word_count * 16;
                input_datas[name] = std::vector<uint8_t>(
                    hbm_data.begin() + base,
                    hbm_data.begin() + base + data_bytes);
            }
        }

        std::vector<uint8_t> golden;
        generate_golden_for_smc(smc, input_datas, golden);

        const uint32_t ur_bank_depth = 256;
        if (golden.size() / 16 < ur_bank_depth) {
            golden.resize(ur_bank_depth * 16, 0);
        }

        write_hex_file(smc_dir + "/output_bank/bank" + std::to_string(desc_.output.bank) + "_final.hex",
                       golden, 16);
    }

    // ===== 落一份 meta_v3.json，供 DDR 真比对脚本(verify_ddr_v3.py)使用 =====
    {
        std::ofstream mf(case_root + "/meta_v3.json");
        if (mf) {
            mf << "{\n";
            mf << "  \"op\": \"" << get_name() << "\",\n";
            mf << "  \"num_smc\": " << static_cast<int>(desc_.num_smc) << ",\n";
            mf << "  \"out_bank\": " << static_cast<int>(desc_.output.bank) << ",\n";
            mf << "  \"out_words\": " << desc_.output.word_count << ",\n";
            mf << "  \"dtype\": \"" << (std::getenv("LSU_DTYPE") ? std::getenv("LSU_DTYPE") : "fp32") << "\",\n";
            mf << "  \"store_byte_base\": " << store_gr_base() << ",\n";
            mf << "  \"word_bytes\": 16,\n";
            mf << "  \"inputs\": [\n";
            {
                size_t i = 0, ni = desc_.inputs.size();
                for (auto& [name, port] : desc_.inputs) {
                    mf << "    {\"name\": \"" << name << "\", \"byte_base\": " << input_gr_base(name)
                       << ", \"words\": " << port.word_count
                       << ", \"word_offset\": " << port.word_offset << "}"
                       << (++i < ni ? "," : "") << "\n";
                }
            }
            mf << "  ]\n";
            mf << "}\n";
        }
    }

    std::cout << "[LsuOpBase] Case assembled: " << case_root << " ("
              << static_cast<int>(desc_.num_smc) << " SMCs)" << std::endl;
    return true;
}

} // namespace lsu
} // namespace aiacc
