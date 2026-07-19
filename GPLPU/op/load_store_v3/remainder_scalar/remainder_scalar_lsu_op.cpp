#include "remainder_scalar_lsu_op.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <random>

namespace aiacc {
namespace lsu {

namespace {

void put_u32(std::vector<uint8_t>& bytes, uint64_t off, uint32_t v) {
    if (bytes.size() < off + 4) bytes.resize(static_cast<size_t>(off + 4), 0);
    bytes[off + 0] = static_cast<uint8_t>(v & 0xFF);
    bytes[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    bytes[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    bytes[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

uint32_t get_u32(const std::vector<uint8_t>& bytes, uint64_t off) {
    uint32_t v = 0;
    if (off + 4 <= bytes.size()) std::memcpy(&v, bytes.data() + off, 4);
    return v;
}

} // namespace

LsuComputeDesc RemainderScalarLsuOp::default_compute_desc() {
    uint16_t wc = 255;
    if (const char* e = std::getenv("LSU_WC")) {
        int v = std::atoi(e);
        if (v > 0 && v <= 255) wc = static_cast<uint16_t>(v);
    }
    LsuComputeDesc d;
    d.inputs["input"] = {0, 0, wc};
    d.inputs["other"] = {1, 0, 1};
    d.inputs["alpha"] = {3, 0, 35};
    d.output = {2, 0, wc};
    d.num_smc = 8;
    d.smc_id_en = 7;
    d.gr_intlv_addr = 0x10000000ULL;
    d.bulk_mode = true;
    return d;
}

std::vector<op::IqInstructionFile> RemainderScalarLsuOp::generate_compute_iq_files() const {
    op::RemainderScalarParams p;
    p.para_n = 1;
    p.para_c = 1;
    p.para_h = 1;
    p.para_w = desc_.output.word_count * 4;
    p.para_dtype = op::RemainderScalarDataType::INT32;
    p.para_scalar_value = 7;
    p.para_smc_id_en = desc_.smc_id_en;

    auto input = desc_.inputs.find("input");
    auto other = desc_.inputs.find("other");
    auto alpha = desc_.inputs.find("alpha");
    if (input != desc_.inputs.end()) {
        p.para_input_bank = input->second.bank;
        p.para_input_addr = static_cast<uint8_t>(input->second.word_offset);
    }
    if (other != desc_.inputs.end()) {
        p.para_scalar_bank = other->second.bank;
        p.para_scalar_addr = static_cast<uint8_t>(other->second.word_offset);
    }
    if (alpha != desc_.inputs.end()) {
        p.para_const_bank = alpha->second.bank;
        p.para_x0_addr = static_cast<uint8_t>(alpha->second.word_offset);
        p.para_const2_addr = static_cast<uint8_t>(alpha->second.word_offset + 1);
        p.para_one_addr = static_cast<uint8_t>(alpha->second.word_offset + 2);
        p.para_zero_addr = static_cast<uint8_t>(alpha->second.word_offset + 3);
        p.para_shift_base_addr = static_cast<uint8_t>(alpha->second.word_offset + 4);
    }
    p.para_output_bank = desc_.output.bank;
    p.para_output_addr = static_cast<uint8_t>(desc_.output.word_offset);

    op::RemainderScalarOp op;
    auto insts = op.compile(p);
    return insts.get_iq_files();
}

void RemainderScalarLsuOp::generate_hbm_data(uint8_t smc_id, uint32_t seed,
                                             std::vector<uint8_t>& hbm_bytes) const {
    uint64_t total = 0;
    for (const auto& kv : desc_.inputs) {
        total = std::max(total, input_gr_base(kv.first) + static_cast<uint64_t>(kv.second.word_count) * 16);
    }
    hbm_bytes.assign(static_cast<size_t>(total), 0);

    std::mt19937 rng(seed + smc_id * 1000);
    std::uniform_int_distribution<int32_t> dist(-4096, 4096);

    for (const auto& kv : desc_.inputs) {
        const std::string& name = kv.first;
        const PortInfo& port = kv.second;
        const uint64_t base = input_gr_base(name);
        const uint32_t elems = static_cast<uint32_t>(port.word_count) * 4;
        for (uint32_t i = 0; i < elems; ++i) {
            uint32_t v = 0;
            if (name == "other") {
                v = 7u;
            } else if (name == "alpha") {
                if (i < 4) {
                    float d = 7.0f;
                    uint32_t bits = 0;
                    std::memcpy(&bits, &d, sizeof(bits));
                    bits = 0x7EF311C7u - bits;
                    std::memcpy(&v, &bits, sizeof(v));
                } else if (i < 8) {
                    float two = 2.0f;
                    std::memcpy(&v, &two, sizeof(v));
                } else if (i < 12) {
                    v = 1u;
                } else if (i < 16) {
                    v = 0u;
                } else {
                    v = static_cast<uint32_t>((i - 16) / 4);
                }
            } else {
                v = static_cast<uint32_t>(dist(rng));
            }
            put_u32(hbm_bytes, base + static_cast<uint64_t>(i) * 4, v);
        }
    }
}

void RemainderScalarLsuOp::generate_golden_for_smc(
    uint8_t /*smc_id*/,
    const std::map<std::string, std::vector<uint8_t>>& input_datas,
    std::vector<uint8_t>& golden_out) const {
    auto it = input_datas.find("input");
    if (it == input_datas.end()) {
        golden_out.clear();
        return;
    }
    const auto& input = it->second;
    const uint32_t words = desc_.output.word_count;
    golden_out.assign(static_cast<size_t>(words) * 16, 0);
    constexpr int32_t scalar = 7;
    const uint32_t elems = std::min<uint32_t>(words * 4, static_cast<uint32_t>(input.size() / 4));
    for (uint32_t i = 0; i < elems; ++i) {
        const int32_t x = static_cast<int32_t>(get_u32(input, static_cast<uint64_t>(i) * 4));
        int64_t q = static_cast<int64_t>(x) / scalar;
        int64_t rem = static_cast<int64_t>(x) % scalar;
        if (rem != 0 && ((rem > 0) != (scalar > 0))) --q;
        const int32_t r = static_cast<int32_t>(static_cast<int64_t>(x) - q * scalar);
        put_u32(golden_out, static_cast<uint64_t>(i) * 4, static_cast<uint32_t>(r));
    }
}

} // namespace lsu
} // namespace aiacc

