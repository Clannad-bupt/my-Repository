#include "div_scalar_op.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "../../iq/iq_id_defs.h"
#include "div_scalar_op_constants.h"
#include "div_scalar_op_ur_inst.h"
#include "ur_code_extractor.h"

namespace aiacc {
namespace op {

namespace {

int32_t get_div_scalar(const app::AppOperatorRequest& req) {
    auto pit = req.para_parameters.find("other_scalar");
    if (pit != req.para_parameters.end() && !pit->second.para_data_i32.empty()) {
        return pit->second.para_data_i32[0];
    }
    auto iit = req.para_inputs.find("other_tensor");
    if (iit != req.para_inputs.end() && !iit->second.para_data_i32.empty()) {
        return iit->second.para_data_i32[0];
    }
    auto sit = req.para_inputs.find("scalar_value");
    if (sit != req.para_inputs.end() && !sit->second.para_data_i32.empty()) {
        return sit->second.para_data_i32[0];
    }
    return 1;
}

DivScalarDataType to_div_scalar_dtype(app::AppDataType dtype) {
    if (dtype != app::AppDataType::INT32) {
        throw std::runtime_error("DivScalar only supports INT32 input/output");
    }
    return DivScalarDataType::INT32;
}

bool get_power_of_two_shift(int32_t scalar, uint8_t& shift) {
    if (scalar <= 0 || (scalar & (scalar - 1)) != 0) return false;
    shift = 0;
    int32_t t = scalar;
    while (t > 1) {
        ++shift;
        t >>= 1;
    }
    return true;
}

int32_t arithmetic_shift_right_i32(int32_t value, uint8_t shift) {
    if (shift == 0) return value;
    const int64_t denom = int64_t{1} << shift;
    if (value >= 0) return static_cast<int32_t>(value / denom);
    return static_cast<int32_t>(-(((-static_cast<int64_t>(value)) + denom - 1) / denom));
}

} // namespace

// ==========================================
// 核心编译逻辑
// ==========================================

std::string DivScalarOp::get_name() const {
    return "DivScalar";
}

OpInstructionsBase* DivScalarOp::compile(const app::AppOperatorRequest& req) {
    if (req.para_layout_mode == "bank_conflict") {
        throw std::runtime_error("DivScalar only supports default layout (no bank conflict)");
    }

    DivScalarParams params;
    params.para_n = req.para_dimensions.para_n;
    params.para_c = req.para_dimensions.para_c;
    params.para_h = req.para_dimensions.para_h;
    params.para_w = req.para_dimensions.para_w;

    if (params.para_n == 0 || params.para_c == 0 || params.para_h == 0 || params.para_w == 0) {
        throw std::runtime_error("DivScalar: invalid dimensions");
    }

    auto input_it = req.para_inputs.find("input_tensor");
    if (input_it != req.para_inputs.end()) {
        params.para_dtype = to_div_scalar_dtype(input_it->second.para_dtype);
    } else if (!req.para_inputs.empty()) {
        const auto& first = req.para_inputs.begin()->second;
        params.para_dtype = to_div_scalar_dtype(first.para_dtype);
    }

    auto get_param = [&](const std::string& key, int def) -> int {
        auto it = req.para_parameters.find(key);
        if (it != req.para_parameters.end() && !it->second.para_data_i32.empty()) {
            return it->second.para_data_i32[0];
        }
        return def;
    };

    params.para_input_bank  = static_cast<uint8_t>(get_param("input_bank", 0));
    params.para_input_addr  = static_cast<uint8_t>(get_param("input_addr", 0));
    params.para_scalar_bank = static_cast<uint8_t>(get_param("other_bank", 1));
    params.para_scalar_addr = static_cast<uint8_t>(get_param("other_addr", 0));
    params.para_output_bank = static_cast<uint8_t>(get_param("output_bank", 2));
    params.para_output_addr = static_cast<uint8_t>(get_param("output_addr", 0));
    params.para_scalar_value = get_div_scalar(req);
    params.para_smc_id_en = DIV_SCALAR_DEFAULT_SMC_ID_EN;

    uint8_t shift = 0;
    if (!get_power_of_two_shift(params.para_scalar_value, shift)) {
        throw std::runtime_error("DivScalar INT32 currently supports only positive power-of-two scalar shift path");
    }
    params.para_shift_amount = shift;

    if (params.para_input_bank == params.para_scalar_bank ||
        params.para_input_bank == params.para_output_bank ||
        params.para_scalar_bank == params.para_output_bank) {
        throw std::runtime_error("DivScalar requires distinct input/scalar/output banks");
    }

    DivScalarInstructions* insts = new DivScalarInstructions();
    *insts = compile(params);
    return insts;
}

DivScalarInstructions DivScalarOp::compile(const DivScalarParams& params) {
    DivScalarInstructions result;
    result.para_dtype = params.para_dtype;
    result.para_smc_id_en = params.para_smc_id_en;

    const uint32_t total_elems = params.para_n * params.para_c * params.para_h * params.para_w;
    const uint32_t elems_per_word = params.get_elems_per_word();
    const uint32_t total_steps = (total_elems + elems_per_word - 1) / elems_per_word;

    if (total_steps == 0) {
        throw std::runtime_error("DivScalar: total_steps is zero");
    }

    result.para_total_steps = total_steps;
    result.para_total_elements = total_elems;

    constexpr uint32_t kBatch = 4;
    result.para_total_cycles = DIV_SCALAR_LOGIC_LATENCY + (total_steps - 1) * kBatch;

    iq::UrCru ur_read_cru, ur_write_cru;
    ds_gen_ur_read_inst(params, 0, ur_read_cru);
    ds_gen_ur_write_inst(params, 0, ur_write_cru);
    result.para_ur_read_crus.push_back(ur_read_cru);
    result.para_ur_write_crus.push_back(ur_write_cru);

    result.para_rtovr_routes.push_back({DIV_SCALAR_RTOVR_OUT_DVR_LOGIC_S0, DIV_SCALAR_RTOVR_IN_UR_RDATA_0});
    result.para_rtovr_routes.push_back({DIV_SCALAR_RTOVR_OUT_DVR_LOGIC_S1, DIV_SCALAR_RTOVR_IN_UR_RDATA_1});
    result.para_rtovr_routes.push_back({DIV_SCALAR_RTOVR_OUT_UVR_WDATA_0, DIV_SCALAR_RTOVR_IN_DR_LOGIC_D});

    iq::LogicalCru logic_cru;
    logic_cru.para_inst_valid = true;
    logic_cru.para_precision_32b = true;
    logic_cru.para_opcode = iq::LogicalOp::ARITH_RIGHT_SHIFT;
    result.para_compute_inst = iq::LogicalIq::pack_cru_logical(logic_cru);
    result.para_compute_code_bits = 6;

    std::cout << "[DivScalar] Bank config:\n"
              << "  Input:  bank=" << static_cast<int>(params.para_input_bank)
              << ", addr=" << static_cast<int>(params.para_input_addr) << "\n"
              << "  Scalar: bank=" << static_cast<int>(params.para_scalar_bank)
              << ", addr=" << static_cast<int>(params.para_scalar_addr)
              << " (shift amount, fixed-read)" << "\n"
              << "  Output: bank=" << static_cast<int>(params.para_output_bank)
              << ", addr=" << static_cast<int>(params.para_output_addr) << "\n"
              << "  Path:   " << "LOGIC arithmetic right shift"
              << ", smc_id_en=" << static_cast<int>(params.para_smc_id_en) << "\n";

    return result;
}

// ==========================================
// get_iq_files (借鉴 bitwise_and)
// ==========================================

std::vector<IqInstructionFile> DivScalarInstructions::get_iq_files() const {
    std::vector<IqInstructionFile> files;

    constexpr uint32_t BITS_EMBED = 3;
    constexpr uint32_t BITS_EMBED_END = 6;
    constexpr uint32_t BITS_LOOPS = 10;
    constexpr uint32_t BITS_COND = 7;
    constexpr uint32_t BITS_DLY = 3;
    constexpr uint32_t BITS_INC = 4;
    constexpr uint32_t BITS_INC_EMBED = 6;
    constexpr uint32_t MASK_EMBED = (1u << BITS_EMBED) - 1;
    constexpr uint32_t MASK_EMBED_END = (1u << BITS_EMBED_END) - 1;
    constexpr uint32_t MASK_LOOPS = (1u << BITS_LOOPS) - 1;
    constexpr uint32_t MASK_COND = (1u << BITS_COND) - 1;
    constexpr uint32_t MASK_DLY = (1u << BITS_DLY) - 1;
    constexpr uint32_t MASK_INC = (1u << BITS_INC) - 1;
    constexpr uint32_t MASK_INC_EMBED = (1u << BITS_INC_EMBED) - 1;

    auto serialize_header_bits = [&](uint8_t vld, uint8_t jump, uint8_t embed, uint16_t loops,
                                     uint8_t cond, uint8_t dly, uint8_t inc,
                                     uint8_t embed_end_bits, uint8_t inc_embed_bits) -> uint64_t {
        uint64_t bits = 0;
        uint32_t pos = 0;
        const uint8_t embed_end = (embed == 0) ? (embed_end_bits & MASK_EMBED_END) : 0u;
        const uint8_t inc_embed = (embed == 0) ? (inc_embed_bits & MASK_INC_EMBED) : 0u;

        bits |= static_cast<uint64_t>(inc_embed & MASK_INC_EMBED) << pos; pos += BITS_INC_EMBED;
        bits |= static_cast<uint64_t>(inc & MASK_INC) << pos; pos += BITS_INC;
        bits |= static_cast<uint64_t>(dly & MASK_DLY) << pos; pos += BITS_DLY;
        bits |= static_cast<uint64_t>(cond & MASK_COND) << pos; pos += BITS_COND;
        bits |= static_cast<uint64_t>(loops & MASK_LOOPS) << pos; pos += BITS_LOOPS;
        bits |= static_cast<uint64_t>(embed_end & MASK_EMBED_END) << pos; pos += BITS_EMBED_END;
        bits |= static_cast<uint64_t>(embed & MASK_EMBED) << pos; pos += BITS_EMBED;
        bits |= static_cast<uint64_t>(jump & 1u) << pos; pos += 1;
        bits |= static_cast<uint64_t>(vld & 1u) << pos;
        return bits;
    };

    auto serialize_header = [&](uint8_t vld, uint8_t jump, uint8_t embed, uint16_t loops,
                                uint8_t cond, uint8_t dly, uint8_t inc,
                                uint8_t embed_end_bits, uint8_t inc_embed_bits) -> std::vector<uint8_t> {
        const uint64_t bits = serialize_header_bits(vld, jump, embed, loops, cond, dly, inc,
                                                    embed_end_bits, inc_embed_bits);
        std::vector<uint8_t> bytes;
        for (int i = 5; i >= 0; --i) {
            bytes.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
        }
        return bytes;
    };

    auto serialize_code = [](uint64_t code, uint16_t code_bits) -> std::vector<uint8_t> {
        std::vector<uint8_t> bytes;
        const uint16_t num_bytes = (code_bits + 7) / 8;
        for (int i = num_bytes - 1; i >= 0; --i) {
            bytes.push_back(static_cast<uint8_t>((code >> (i * 8)) & 0xFF));
        }
        return bytes;
    };

    auto make_instruction = [&](uint8_t vld, uint8_t jump, uint8_t embed, uint16_t loops,
                                uint8_t cond, uint8_t dly, int8_t inc,
                                uint8_t embed_end_bits, uint8_t inc_embed_bits,
                                const std::vector<uint8_t>& code_bytes,
                                int code_bits = -1) -> std::vector<uint8_t> {
        if (code_bits > 0 && code_bits <= 23) {
            const uint64_t h = serialize_header_bits(vld, jump, embed, loops, cond, dly,
                                                     static_cast<uint8_t>(inc & 0xF),
                                                     embed_end_bits, inc_embed_bits);
            uint64_t c = 0;
            for (uint8_t b : code_bytes) {
                c = (c << 8) | b;
            }
            if (code_bits < 64) {
                c &= ((1ULL << code_bits) - 1ULL);
            }
            const uint64_t total = (h << code_bits) | c;
            const int total_width = 41 + code_bits;
            const int num_bytes = (total_width + 7) / 8;
            std::vector<uint8_t> ret;
            for (int i = num_bytes - 1; i >= 0; --i) {
                ret.push_back(static_cast<uint8_t>((total >> (i * 8)) & 0xFF));
            }
            return ret;
        }

        auto header = serialize_header(vld, jump, embed, loops, cond, dly,
                                       static_cast<uint8_t>(inc & 0xF),
                                       embed_end_bits, inc_embed_bits);
        header.insert(header.end(), code_bytes.begin(), code_bytes.end());
        return header;
    };

    auto serialize_ur_wdata_inst = [&](uint8_t vld, uint8_t jump, uint8_t embed, uint16_t loops,
                                       uint8_t cond, uint8_t dly, uint8_t inc,
                                       uint8_t embed_end_bits, uint8_t inc_embed_bits,
                                       uint64_t code, uint16_t code_bits) -> std::vector<uint8_t> {
        const uint64_t header = serialize_header_bits(vld, jump, embed, loops, cond, dly, inc,
                                                      embed_end_bits, inc_embed_bits);
        const uint64_t mask = (code_bits >= 64) ? ~0ULL : ((1ULL << code_bits) - 1ULL);
        const unsigned __int128 inst =
            (static_cast<unsigned __int128>(header) << code_bits) |
            static_cast<unsigned __int128>(code & mask);

        std::vector<uint8_t> bytes;
        const int total_width = 41 + code_bits;
        const int num_bytes = (total_width + 7) / 8;
        for (int i = num_bytes - 1; i >= 0; --i) {
            bytes.push_back(static_cast<uint8_t>((inst >> (i * 8)) & 0xFF));
        }
        return bytes;
    };
    constexpr uint8_t kMaxDlyField = 7;

    const uint16_t loops_inner = (para_total_steps >= 1) ? static_cast<uint16_t>(para_total_steps - 1) : 0;
    const uint8_t inc_r0 = 1u;  // tensor: address increments
    const uint8_t inc_r1 = 0u;  // scalar: address fixed (broadcast)
    const uint8_t inc_w0 = 1u;  // output: address increments
    const uint8_t smc_id_en = para_smc_id_en & 0x1Fu;

    // 1. UR Read Port 0 (Tensor input, INC=1)
    {
        IqInstructionFile file;
        file.para_iq_id = iq::IQ_ID_UR_RDATA_0;
        file.para_iq_name = iq::IQ_NAME_UR_RDATA_0;
        file.para_width_bits = 41 + 17;

        const uint64_t code = static_cast<uint64_t>(extract_ur_read_port0_code(para_ur_read_crus[0])) |
                              (static_cast<uint64_t>(smc_id_en) << 12);
        const auto code_bytes = serialize_code(code, 17);

        uint32_t total_dly = 0;
        const uint8_t dly = static_cast<uint8_t>(total_dly);
        auto inst = make_instruction(1, 0, 0, loops_inner, 0, dly, inc_r0, 0, 0, code_bytes, 17);
        file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
        files.push_back(file);
    }
    // 2. UR Read Port 1 (Scalar or shift amount, INC=0 broadcast)
    {
        IqInstructionFile file;
        file.para_iq_id = iq::IQ_ID_UR_RDATA_1;
        file.para_iq_name = iq::IQ_NAME_UR_RDATA_1;
        file.para_width_bits = 41 + 17;

        const uint64_t code = static_cast<uint64_t>(extract_ur_read_port1_code(para_ur_read_crus[0])) |
                              (static_cast<uint64_t>(smc_id_en) << 12);
        const auto code_bytes = serialize_code(code, 17);

        uint32_t total_dly = 0;
        const uint8_t dly = static_cast<uint8_t>(total_dly);
        auto inst = make_instruction(1, 0, 0, loops_inner, 0, dly, inc_r1, 0, 0, code_bytes, 17);
        file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
        files.push_back(file);
    }
    // 3. RtoVR Routing
    for (const auto& route : para_rtovr_routes) {
        const uint8_t target_port = route.first;
        const uint8_t src_id = route.second;
        const bool is_read_route =
            (target_port == DIV_SCALAR_RTOVR_OUT_DVR_LOGIC_S0 || target_port == DIV_SCALAR_RTOVR_OUT_DVR_LOGIC_S1);

        IqInstructionFile file;
        file.para_iq_id = iq::IQ_ID_RTOVR_BASE + target_port;
        file.para_iq_name = "rtovr-" + std::to_string(target_port);
        file.para_width_bits = 41 + 7;

        const uint8_t route_code = static_cast<uint8_t>((src_id << 1) | 1u);
        const auto code_bytes = serialize_code(route_code, 7);

        uint32_t total_dly = is_read_route ? 1u : 3u;
        uint8_t dly = static_cast<uint8_t>(total_dly);
        auto inst = make_instruction(1, 0, 0, loops_inner, 0, dly, 0, 0, 0, code_bytes, 7);
        file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
        files.push_back(file);
    }
    // 4. LOGIC arithmetic right shift compute.
    {
        IqInstructionFile file;
        file.para_iq_id = iq::IQ_ID_LOGIC;
        file.para_iq_name = iq::IQ_NAME_LOGIC;
        file.para_width_bits = 41 + 11;
        const uint64_t code = (static_cast<uint64_t>(smc_id_en) << 6) | (para_compute_inst & 0x3Fu);
        const auto code_bytes = serialize_code(code, 11);
        const uint8_t dly = 2;
        auto inst = make_instruction(1, 0, 0, loops_inner, 0, dly, 0, 0, 0, code_bytes, 11);
        file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
        files.push_back(file);
    }
    // 5. UR Write Port 0 (Result, INC=1)
    {
        IqInstructionFile file;
        file.para_iq_id = iq::IQ_ID_UR_WDATA_0;
        file.para_iq_name = iq::IQ_NAME_UR_WDATA_0;
        file.para_width_bits = 41 + 33;

        const uint64_t code = static_cast<uint64_t>(extract_ur_write_port0_code(para_ur_write_crus[0])) |
                              (static_cast<uint64_t>(smc_id_en) << 28);

        uint32_t total_dly = 4u;
        const uint8_t dly = static_cast<uint8_t>(total_dly);
        auto inst = serialize_ur_wdata_inst(1, 0, 0, loops_inner, 0, dly, inc_w0, 0, 0, code, 33);
        file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
        files.push_back(file);
    }
    return files;
}

// ==========================================
// 验证与数据生�?// ==========================================

bool DivScalarOp::calculate_golden(const app::AppOperatorRequest& req,
                                   std::vector<uint8_t>& golden_bin) {
    golden_bin.clear();

    auto it = req.para_inputs.find("input_tensor");
    if (it == req.para_inputs.end()) it = req.para_inputs.find("input");
    if (it == req.para_inputs.end() || it->second.para_dtype != app::AppDataType::INT32) return false;

    const int32_t scalar = get_div_scalar(req);
    uint8_t shift = 0;
    if (!get_power_of_two_shift(scalar, shift)) return false;

    auto push_u32 = [&](uint32_t v) {
        golden_bin.push_back(v & 0xFF);
        golden_bin.push_back((v >> 8) & 0xFF);
        golden_bin.push_back((v >> 16) & 0xFF);
        golden_bin.push_back((v >> 24) & 0xFF);
    };

    const auto& input = it->second;
    if (!input.para_data_i32.empty()) {
        for (int32_t x : input.para_data_i32) {
            push_u32(static_cast<uint32_t>(arithmetic_shift_right_i32(x, shift)));
        }
    }
    else {
        return false;
    }
    return true;
}
void DivScalarOp::fill_random_request(app::AppOperatorRequest& req,
                                      std::mt19937& rng,
                                      std::uniform_real_distribution<float>& data_dist,
                                      const std::string& dtype_str) {
    (void)dtype_str;
    req.para_operator = "DivScalar";
    if (req.para_layout_mode == "bank_conflict") req.para_layout_mode = "default";

    const uint32_t total = req.para_dimensions.para_n * req.para_dimensions.para_c *
                           req.para_dimensions.para_h * req.para_dimensions.para_w;

    app::AppTensorDesc input_desc;
    input_desc.para_name = "input_tensor";
    input_desc.para_dtype = app::AppDataType::INT32;
    input_desc.para_shape = {req.para_dimensions.para_n, req.para_dimensions.para_c,
                             req.para_dimensions.para_h, req.para_dimensions.para_w};
    input_desc.para_data_i32.reserve(total);
    for (uint32_t i = 0; i < total; ++i) {
        input_desc.para_data_i32.push_back(static_cast<int32_t>(std::round(data_dist(rng) * 64.0f)));
    }
    req.para_inputs["input_tensor"] = input_desc;

    app::AppTensorDesc scalar_desc;
    scalar_desc.para_name = "other_scalar";
    scalar_desc.para_dtype = app::AppDataType::INT32;
    scalar_desc.para_shape = {1};
    scalar_desc.para_data_i32.push_back(16);
    req.para_parameters["other_scalar"] = scalar_desc;

    auto set_i32_param = [&](const std::string& name, int v) {
        app::AppTensorDesc d;
        d.para_name = name;
        d.para_dtype = app::AppDataType::INT32;
        d.para_shape = {1};
        d.para_data_i32.push_back(v);
        req.para_parameters[name] = d;
    };
    set_i32_param("input_bank", 0);
    set_i32_param("input_addr", 0);
    set_i32_param("other_bank", 1);
    set_i32_param("other_addr", 0);
    set_i32_param("output_bank", 2);
    set_i32_param("output_addr", 0);
}
bool DivScalarOp::generate_data(const app::AppOperatorRequest& req,
                                std::vector<uint8_t>& input_bin,
                                std::vector<uint8_t>& other_bin,
                                std::vector<uint8_t>& alpha_bin) {
    input_bin.clear();
    other_bin.clear();
    alpha_bin.clear();

    auto it = req.para_inputs.find("input_tensor");
    if (it == req.para_inputs.end()) it = req.para_inputs.find("input");
    if (it == req.para_inputs.end() || it->second.para_dtype != app::AppDataType::INT32) return false;

    const int32_t scalar = get_div_scalar(req);
    uint8_t shift = 0;
    if (!get_power_of_two_shift(scalar, shift)) return false;

    auto push_to = [](std::vector<uint8_t>& out, uint32_t v) {
        out.push_back(v & 0xFF);
        out.push_back((v >> 8) & 0xFF);
        out.push_back((v >> 16) & 0xFF);
        out.push_back((v >> 24) & 0xFF);
    };

    const auto& input = it->second;
    if (!input.para_data_i32.empty()) {
        for (int32_t v : input.para_data_i32) push_to(input_bin, static_cast<uint32_t>(v));
    }
    else {
        return false;
    }

    constexpr int kInt32LanesPerWord = 4;
    for (int lane = 0; lane < kInt32LanesPerWord; ++lane) {
        push_to(other_bin, static_cast<uint32_t>(shift));
    }
    return true;
}
std::vector<uint8_t> DivScalarInstructions::serialize() const {
    std::vector<uint8_t> result;
    auto push_u32 = [&](uint32_t v) {
        result.push_back(v & 0xFF);
        result.push_back((v >> 8) & 0xFF);
        result.push_back((v >> 16) & 0xFF);
        result.push_back((v >> 24) & 0xFF);
    };

    push_u32(para_total_steps);
    push_u32(para_total_elements);
    push_u32(para_total_cycles);
    result.push_back(static_cast<uint8_t>(para_compute_inst & 0xFF));

    for (const auto& r : para_rtovr_routes) {
        result.push_back(r.first);
        result.push_back(r.second);
    }

    for (const auto& cru : para_ur_read_crus) {
        uint32_t ur_words[6];
        iq::UrIq::pack_cru_ur_words(cru, ur_words);
        for (int i = 0; i < 6; ++i) {
            push_u32(ur_words[i]);
        }
    }

    for (const auto& cru : para_ur_write_crus) {
        uint32_t ur_words[6];
        iq::UrIq::pack_cru_ur_words(cru, ur_words);
        for (int i = 0; i < 6; ++i) {
            push_u32(ur_words[i]);
        }
    }

    return result;
}

void print_div_scalar_instructions(const DivScalarInstructions& insts) {
    std::cout << "=== DivScalarInstructions ===\n"
              << "  total_steps:    " << insts.para_total_steps << "\n"
              << "  total_elements: " << insts.para_total_elements << "\n"
              << "  total_cycles:   " << insts.para_total_cycles << "\n"
              << "  ur_read_crus:   " << insts.para_ur_read_crus.size() << "\n"
              << "  ur_write_crus:  " << insts.para_ur_write_crus.size() << "\n"
              << "  rtovr_routes:   " << insts.para_rtovr_routes.size() << "\n"
              << "  path:           logic_shift" << "\n"
              << "  compute_inst:   0x" << std::hex << static_cast<int>(insts.para_compute_inst)
              << std::dec << "\n";
}
REGISTER_OP(DivScalarOp);

} // namespace op
} // namespace aiacc

