#include "sub_lsu_op.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

#include "../../app/fp16_utils.h"

namespace aiacc {
namespace lsu {

namespace {

bool env_flag(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] != '\0' && std::string(v) != "0";
}

std::string env_string(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

uint64_t env_u64(const char* name, uint64_t fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    return static_cast<uint64_t>(std::strtoull(v, nullptr, 0));
}

bool dtype_is_fp16() {
    std::string dtype = env_string("LSU_DTYPE");
    std::transform(dtype.begin(), dtype.end(), dtype.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return dtype == "fp16" || dtype == "float16" || (!env_string("LLAMA_SUB_DIR").empty() && dtype.empty());
}

uint32_t elems_per_word() {
    return dtype_is_fp16() ? 8u : 4u;
}

std::string find_named_bin(const std::string& dir, const std::string& suffix) {
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (dirent* ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name.size() >= suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            found = dir + "/" + name;
            break;
        }
    }
    closedir(d);
    return found;
}

std::vector<uint8_t> read_file_slice(const std::string& path, uint64_t byte_off, uint64_t bytes) {
    std::vector<uint8_t> out(static_cast<size_t>(bytes), 0);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error("failed to open " + path);
    }
    ifs.seekg(0, std::ios::end);
    const uint64_t size = static_cast<uint64_t>(ifs.tellg());
    if (byte_off >= size) return out;
    const uint64_t n = std::min<uint64_t>(bytes, size - byte_off);
    ifs.seekg(static_cast<std::streamoff>(byte_off), std::ios::beg);
    ifs.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
    return out;
}

uint64_t file_size_bytes(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return 0;
    return static_cast<uint64_t>(ifs.tellg());
}

uint64_t align_up(uint64_t value, uint64_t align) {
    if (align == 0) return value;
    return ((value + align - 1u) / align) * align;
}

void put_u16(std::vector<uint8_t>& bytes, uint64_t off, uint16_t v) {
    if (bytes.size() < off + 2) bytes.resize(static_cast<size_t>(off + 2), 0);
    bytes[off + 0] = static_cast<uint8_t>(v & 0xFF);
    bytes[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void put_u32(std::vector<uint8_t>& bytes, uint64_t off, uint32_t v) {
    if (bytes.size() < off + 4) bytes.resize(static_cast<size_t>(off + 4), 0);
    bytes[off + 0] = static_cast<uint8_t>(v & 0xFF);
    bytes[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    bytes[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    bytes[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

uint16_t get_u16(const std::vector<uint8_t>& bytes, uint64_t off) {
    uint16_t v = 0;
    if (off + 2 <= bytes.size()) std::memcpy(&v, bytes.data() + off, 2);
    return v;
}

uint32_t get_u32(const std::vector<uint8_t>& bytes, uint64_t off) {
    uint32_t v = 0;
    if (off + 4 <= bytes.size()) std::memcpy(&v, bytes.data() + off, 4);
    return v;
}

uint16_t ftz_fp16_bits(uint16_t h) {
    return ((h & 0x7C00u) == 0u) ? static_cast<uint16_t>(h & 0x8000u) : h;
}

void write_alpha_word(std::vector<uint8_t>& bytes, uint64_t off, bool fp16, bool one) {
    if (fp16) {
        const uint16_t h = app::float32_to_float16(one ? 1.0f : 0.75f);
        for (uint32_t lane = 0; lane < 8; ++lane) put_u16(bytes, off + lane * 2, h);
    } else {
        const float f = one ? 1.0f : 0.75f;
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        for (uint32_t lane = 0; lane < 4; ++lane) put_u32(bytes, off + lane * 4, bits);
    }
}

uint64_t full_ddr_smc_word_offset(const LsuComputeDesc& desc, uint8_t smc_id) {
    const uint64_t stride = env_u64("SUB_SMC_WORD_STRIDE", desc.output.word_count);
    return desc.full_word_offset + static_cast<uint64_t>(smc_id) * stride;
}

} // namespace

LsuComputeDesc SubLsuOp::default_compute_desc() {
    uint16_t wc = 256;
    if (const char* e = std::getenv("LSU_WC")) {
        int v = std::atoi(e);
        if (v <= 0 || v > 256) {
            throw std::runtime_error("LSU_WC must be in [1,256]; 256 is emitted as two 128-word LDB/STB transfers");
        }
        wc = static_cast<uint16_t>(v);
    }

    LsuComputeDesc d;
    d.inputs["input"] = {0, 0, wc};
    d.inputs["other"] = {1, 0, wc};
    d.inputs["alpha"] = {2, 0, 1};
    d.output = {3, 0, wc};
    d.num_smc = static_cast<uint8_t>(env_u64("SUB_NUM_SMC", 8) & 0xFFu);
    if (d.num_smc == 0 || d.num_smc > 8) {
        throw std::runtime_error("SUB_NUM_SMC must be in [1,8]");
    }
    d.smc_id_en = static_cast<uint8_t>(env_u64("SUB_SMC_ID_EN", d.num_smc - 1) & 0x1Fu);
    d.gr_intlv_addr = 0x10000000ULL;
    d.bulk_mode = true;
    d.store_start_delay = static_cast<uint32_t>(env_u64("SUB_STORE_START_DELAY", 32));
    d.store_start_cond = static_cast<uint8_t>(env_u64("SUB_STORE_START_COND", 26) & 0x7F);
    if (const char* e = std::getenv("LSU_BULK_MODE")) {
        d.bulk_mode = (std::string(e) != "0");
    }
    d.full_ddr_mode = env_flag("SUB_FULL_DDR");
    if (d.full_ddr_mode) {
        const std::string llama_dir = env_string("LLAMA_SUB_DIR");
        uint64_t total_words = env_u64("SUB_TOTAL_WORDS", 0);
        if (total_words == 0 && !llama_dir.empty()) {
            const std::string self_bin = find_named_bin(llama_dir, "-self.bin");
            const uint64_t self_bytes = self_bin.empty() ? 0 : file_size_bytes(self_bin);
            total_words = self_bytes / 16u;
        }
        if (total_words == 0) total_words = wc;

        const uint64_t total_bytes = total_words * 16u;
        const uint64_t alpha_base = env_u64("SUB_ALPHA_FULL_BASE", 0x00000000ULL);
        const uint64_t input_base = env_u64("SUB_INPUT_FULL_BASE", 0x00100000ULL);
        const uint64_t other_base_default = align_up(input_base + total_bytes, 0x1000ULL);
        const uint64_t other_base = env_u64("SUB_OTHER_FULL_BASE", other_base_default);
        const uint64_t output_base_default = align_up(other_base + total_bytes, 0x1000ULL);
        const uint64_t output_base = env_u64("SUB_OUTPUT_FULL_BASE", output_base_default);

        d.full_word_offset = env_u64("LLAMA_WORD_OFFSET", env_u64("LLAMA_OFFSET_WORD", 0));
        d.full_total_words = total_words;
        d.full_input_byte_base["alpha"] = alpha_base;
        d.full_input_byte_base["input"] = input_base;
        d.full_input_byte_base["other"] = other_base;
        d.full_output_byte_base = output_base;
    }
    return d;
}

std::vector<op::IqInstructionFile> SubLsuOp::generate_compute_iq_files() const {
    op::SubOpParams params;
    params.para_n = 1;
    params.para_c = 1;
    params.para_h = 1;
    params.para_w = desc_.output.word_count * elems_per_word();
    params.para_dtype = dtype_is_fp16() ? op::SubDataType::FP16 : op::SubDataType::FP32;

    auto input = desc_.inputs.find("input");
    auto other = desc_.inputs.find("other");
    auto alpha = desc_.inputs.find("alpha");
    if (input != desc_.inputs.end()) {
        params.para_input_bank = input->second.bank;
        params.para_input_addr = static_cast<uint8_t>(input->second.word_offset);
    }
    if (other != desc_.inputs.end()) {
        params.para_other_bank = other->second.bank;
        params.para_other_addr = static_cast<uint8_t>(other->second.word_offset);
    }
    if (alpha != desc_.inputs.end()) {
        params.para_alpha_bank = alpha->second.bank;
        params.para_alpha_addr = static_cast<uint8_t>(alpha->second.word_offset);
    }
    params.para_output_bank = desc_.output.bank;
    params.para_output_addr = static_cast<uint8_t>(desc_.output.word_offset);

    op::SubOp sub_op;
    auto insts = sub_op.compile(params);
    return insts.get_iq_files();
}

void SubLsuOp::generate_hbm_data(uint8_t smc_id, uint32_t seed,
                                 std::vector<uint8_t>& hbm_bytes) const {
    const bool fp16 = dtype_is_fp16();
    const std::string llama_dir = env_string("LLAMA_SUB_DIR");

    if (desc_.full_ddr_mode) {
        const uint64_t total_words = desc_.full_total_words ? desc_.full_total_words : desc_.output.word_count;
        const uint64_t total_bytes = total_words * 16u;
        const uint64_t alpha_base = desc_.full_input_byte_base.at("alpha");
        const uint64_t input_base = desc_.full_input_byte_base.at("input");
        const uint64_t other_base = desc_.full_input_byte_base.at("other");
        const uint64_t output_base = desc_.full_output_byte_base;

        uint64_t total = 0;
        total = std::max(total, alpha_base + 16u);
        total = std::max(total, input_base + total_bytes);
        total = std::max(total, other_base + total_bytes);
        total = std::max(total, output_base + total_bytes);
        hbm_bytes.assign(static_cast<size_t>(total), 0);

        if (!llama_dir.empty()) {
            const std::string self_bin = find_named_bin(llama_dir, "-self.bin");
            const std::string other_bin = find_named_bin(llama_dir, "-other.bin");
            if (self_bin.empty() || other_bin.empty()) {
                throw std::runtime_error("LLAMA_SUB_DIR must contain *-self.bin and *-other.bin");
            }

            const uint64_t smc_word_off = full_ddr_smc_word_offset(desc_, smc_id);
            const uint64_t smc_byte_off = smc_word_off * 16u;
            const uint64_t data_bytes = static_cast<uint64_t>(desc_.output.word_count) * 16u;
            if (smc_word_off + desc_.output.word_count > total_words) {
                throw std::runtime_error("SMC LLAMA slice exceeds tensor words");
            }

            const auto self_data = read_file_slice(self_bin, smc_byte_off, data_bytes);
            const auto other_data = read_file_slice(other_bin, smc_byte_off, data_bytes);
            const uint64_t local_byte_off = desc_.full_word_offset * 16u;
            std::copy(self_data.begin(), self_data.end(), hbm_bytes.begin() + input_base + local_byte_off);
            std::copy(other_data.begin(), other_data.end(), hbm_bytes.begin() + other_base + local_byte_off);
            write_alpha_word(hbm_bytes, alpha_base, fp16, true);
            return;
        }
    }

    uint64_t total = 0;
    for (const auto& kv : desc_.inputs) {
        total = std::max(total, input_gr_base(kv.first) + static_cast<uint64_t>(kv.second.word_count) * 16);
    }
    hbm_bytes.assign(static_cast<size_t>(total), 0);

    if (!llama_dir.empty()) {
        const std::string self_bin = find_named_bin(llama_dir, "-self.bin");
        const std::string other_bin = find_named_bin(llama_dir, "-other.bin");
        if (self_bin.empty() || other_bin.empty()) {
            throw std::runtime_error("LLAMA_SUB_DIR must contain *-self.bin and *-other.bin");
        }
        const uint64_t word_off = env_u64("LLAMA_WORD_OFFSET", env_u64("LLAMA_OFFSET_WORD", 0));
        const uint64_t byte_off = env_u64("LLAMA_BYTE_OFFSET", word_off * 16u);
        const uint64_t data_bytes = static_cast<uint64_t>(desc_.output.word_count) * 16u;

        const auto self_data = read_file_slice(self_bin, byte_off, data_bytes);
        const auto other_data = read_file_slice(other_bin, byte_off, data_bytes);
        std::copy(self_data.begin(), self_data.end(), hbm_bytes.begin() + input_gr_base("input"));
        std::copy(other_data.begin(), other_data.end(), hbm_bytes.begin() + input_gr_base("other"));
        write_alpha_word(hbm_bytes, input_gr_base("alpha"), fp16, true);
        return;
    }

    std::mt19937 rng(seed + smc_id * 1000);
    std::uniform_real_distribution<float> data_dist(-8.0f, 8.0f);
    std::uniform_real_distribution<float> alpha_dist(0.1f, 2.0f);
    const bool alpha_one = env_flag("SUB_ALPHA_ONE");

    for (const auto& kv : desc_.inputs) {
        const std::string& name = kv.first;
        const PortInfo& port = kv.second;
        const uint64_t base = input_gr_base(name);
        const uint32_t lanes = fp16 ? 8u : 4u;
        for (uint32_t w = 0; w < port.word_count; ++w) {
            if (name == "alpha") {
                write_alpha_word(hbm_bytes, base + static_cast<uint64_t>(w) * 16u, fp16, alpha_one);
                if (!alpha_one) {
                    for (uint32_t lane = 0; lane < lanes; ++lane) {
                        const float f = alpha_dist(rng);
                        if (fp16) {
                            put_u16(hbm_bytes, base + static_cast<uint64_t>(w) * 16u + lane * 2,
                                    app::float32_to_float16(f));
                        } else {
                            uint32_t bits = 0;
                            std::memcpy(&bits, &f, sizeof(bits));
                            put_u32(hbm_bytes, base + static_cast<uint64_t>(w) * 16u + lane * 4, bits);
                        }
                    }
                }
                continue;
            }
            for (uint32_t lane = 0; lane < lanes; ++lane) {
                const float f = data_dist(rng);
                if (fp16) {
                    put_u16(hbm_bytes, base + static_cast<uint64_t>(w) * 16u + lane * 2,
                            app::float32_to_float16(f));
                } else {
                    uint32_t bits = 0;
                    std::memcpy(&bits, &f, sizeof(bits));
                    put_u32(hbm_bytes, base + static_cast<uint64_t>(w) * 16u + lane * 4, bits);
                }
            }
        }
    }
}

void SubLsuOp::generate_golden_for_smc(
    uint8_t smc_id,
    const std::map<std::string, std::vector<uint8_t>>& input_datas,
    std::vector<uint8_t>& golden_out) const {
    const bool fp16 = dtype_is_fp16();
    const std::string llama_dir = env_string("LLAMA_SUB_DIR");

    if (!llama_dir.empty() && (!fp16 || env_flag("SUB_USE_LLAMA_RETURN_GOLDEN"))) {
        const std::string ret_bin = find_named_bin(llama_dir, "-return.bin");
        if (!ret_bin.empty()) {
            const uint64_t word_off = desc_.full_ddr_mode
                ? full_ddr_smc_word_offset(desc_, smc_id)
                : env_u64("LLAMA_WORD_OFFSET", env_u64("LLAMA_OFFSET_WORD", 0));
            const uint64_t byte_off = env_u64("LLAMA_BYTE_OFFSET", word_off * 16u);
            golden_out = read_file_slice(ret_bin, byte_off, static_cast<uint64_t>(desc_.output.word_count) * 16u);
            return;
        }
    }

    auto it_inp = input_datas.find("input");
    auto it_oth = input_datas.find("other");
    auto it_alp = input_datas.find("alpha");
    if (it_inp == input_datas.end() || it_oth == input_datas.end() || it_alp == input_datas.end()) {
        golden_out.clear();
        return;
    }

    const auto& inp = it_inp->second;
    const auto& oth = it_oth->second;
    const auto& alp = it_alp->second;
    golden_out.assign(static_cast<size_t>(desc_.output.word_count) * 16u, 0);

    if (fp16) {
        const uint32_t elems = std::min<uint32_t>(desc_.output.word_count * 8u,
            std::min<uint32_t>(static_cast<uint32_t>(inp.size() / 2), static_cast<uint32_t>(oth.size() / 2)));
        for (uint32_t i = 0; i < elems; ++i) {
            const uint16_t a_bits = get_u16(inp, static_cast<uint64_t>(i) * 2u);
            const uint16_t b_bits = get_u16(oth, static_cast<uint64_t>(i) * 2u);
            const uint16_t al_bits = get_u16(alp, static_cast<uint64_t>(i % 8u) * 2u);
            const double a = static_cast<double>(app::float16_to_float32(a_bits));
            const double b = static_cast<double>(app::float16_to_float32(b_bits));
            const double al = static_cast<double>(app::float16_to_float32(al_bits));
            const uint16_t mul_bits = app::float32_to_float16(static_cast<float>(al * b));
            const float mul = app::float16_to_float32(mul_bits);
            const uint16_t out = app::float32_to_float16(static_cast<float>(a - static_cast<double>(mul)));
            put_u16(golden_out, static_cast<uint64_t>(i) * 2u, out);
        }
        return;
    }

    const uint32_t elems = std::min<uint32_t>(desc_.output.word_count * 4u,
        std::min<uint32_t>(static_cast<uint32_t>(inp.size() / 4), static_cast<uint32_t>(oth.size() / 4)));
    for (uint32_t i = 0; i < elems; ++i) {
        const uint32_t a_bits = get_u32(inp, static_cast<uint64_t>(i) * 4u);
        const uint32_t b_bits = get_u32(oth, static_cast<uint64_t>(i) * 4u);
        const uint32_t al_bits = get_u32(alp, static_cast<uint64_t>(i % 4u) * 4u);
        float a = 0.0f, b = 0.0f, al = 0.0f;
        std::memcpy(&a, &a_bits, 4);
        std::memcpy(&b, &b_bits, 4);
        std::memcpy(&al, &al_bits, 4);
        float mul = al * b;
        uint32_t mul_bits = 0;
        std::memcpy(&mul_bits, &mul, 4);
        if ((mul_bits & 0x7F800000u) == 0u) {
            mul_bits &= 0x80000000u;
            std::memcpy(&mul, &mul_bits, 4);
        }
        const float out_f = a - mul;
        uint32_t out_bits = 0;
        std::memcpy(&out_bits, &out_f, 4);
        put_u32(golden_out, static_cast<uint64_t>(i) * 4u, out_bits);
    }
}

} // namespace lsu
} // namespace aiacc
