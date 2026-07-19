#include "remainder_scalar_op.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "../../iq/iq_id_defs.h"

namespace aiacc {
namespace op {

namespace {

constexpr uint8_t RTOVR_OUT_DVR_INTADD_S0 = 0;
constexpr uint8_t RTOVR_OUT_DVR_INTADD_S1 = 1;
constexpr uint8_t RTOVR_OUT_DVR_FPMUL_S0 = 3;
constexpr uint8_t RTOVR_OUT_DVR_FPMUL_S1 = 4;
constexpr uint8_t RTOVR_OUT_DVR_FPADD_S0 = 5;
constexpr uint8_t RTOVR_OUT_DVR_FPADD_S1 = 6;
constexpr uint8_t RTOVR_OUT_DVR_FPTOINT_S = 9;
constexpr uint8_t RTOVR_OUT_DVR_INTTOFP_S = 10;
constexpr uint8_t RTOVR_OUT_DVR_LOGIC_S0 = 11;
constexpr uint8_t RTOVR_OUT_DVR_LOGIC_S1 = 12;
constexpr uint8_t RTOVR_OUT_DVR_LOGIC_ST = 13;
constexpr uint8_t RTOVR_OUT_UVR_WDATA_0 = 35;

constexpr uint8_t RTOVR_IN_DR_INTADD_D0 = 0;
constexpr uint8_t RTOVR_IN_DR_INTADD_ST = 2;
constexpr uint8_t RTOVR_IN_DR_FPMUL_D = 3;
constexpr uint8_t RTOVR_IN_DR_FPADD_D = 4;
constexpr uint8_t RTOVR_IN_DR_FPADD_ST = 5;
constexpr uint8_t RTOVR_IN_DR_FPTOINT_D = 8;
constexpr uint8_t RTOVR_IN_DR_INTTOFP_D = 9;
constexpr uint8_t RTOVR_IN_DR_LOGIC_D = 10;
constexpr uint8_t RTOVR_IN_UR_RDATA_0 = 30;
constexpr uint8_t RTOVR_IN_UR_RDATA_1 = 31;
constexpr uint8_t RTOVR_IN_UR_RDATA_2 = 32;

constexpr uint8_t kMaxDlyField = 7;

RemainderScalarDataType to_dtype(app::AppDataType dtype) {
    if (dtype != app::AppDataType::INT32) {
        throw std::runtime_error("RemainderScalar only supports INT32 input/output");
    }
    return RemainderScalarDataType::INT32;
}

int32_t get_i32_param(const app::AppOperatorRequest& req, const std::string& key, int32_t def) {
    auto it = req.para_parameters.find(key);
    if (it != req.para_parameters.end() && !it->second.para_data_i32.empty()) {
        return it->second.para_data_i32[0];
    }
    return def;
}

int32_t get_scalar(const app::AppOperatorRequest& req) {
    for (const char* key : {"other_scalar", "scalar", "scalar_value"}) {
        auto pit = req.para_parameters.find(key);
        if (pit != req.para_parameters.end() && !pit->second.para_data_i32.empty()) {
            return pit->second.para_data_i32[0];
        }
        auto iit = req.para_inputs.find(key);
        if (iit != req.para_inputs.end() && !iit->second.para_data_i32.empty()) {
            return iit->second.para_data_i32[0];
        }
    }
    auto it = req.para_inputs.find("other_tensor");
    if (it != req.para_inputs.end() && !it->second.para_data_i32.empty()) return it->second.para_data_i32[0];
    return 7;
}

void push_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void push_f32(std::vector<uint8_t>& out, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    push_u32(out, bits);
}

float initial_recip_guess(int32_t scalar) {
    const bool neg = scalar < 0;
    float d = static_cast<float>(neg ? -static_cast<int64_t>(scalar) : scalar);
    uint32_t bits = 0;
    std::memcpy(&bits, &d, sizeof(bits));
    bits = 0x7EF311C7u - bits;
    float x0 = 0.0f;
    std::memcpy(&x0, &bits, sizeof(x0));
    return neg ? -x0 : x0;
}

int64_t floor_div_i64(int64_t x, int64_t y) {
    if (y == 0) throw std::runtime_error("RemainderScalar scalar must not be zero");
    int64_t q = x / y;
    int64_t r = x % y;
    if (r != 0 && ((r > 0) != (y > 0))) --q;
    return q;
}

uint32_t ur_read_code(uint8_t bank, uint32_t addr) {
    return 1u | (static_cast<uint32_t>(bank & 0x7u) << 1) | ((addr & 0xFFu) << 4);
}

uint32_t ur_read_code_smc(uint8_t bank, uint32_t addr, uint8_t smc) {
    return ur_read_code(bank, addr) | (static_cast<uint32_t>(smc & 0x1Fu) << 12);
}

uint64_t ur_write_code_smc(uint8_t bank, uint32_t addr, uint8_t smc, uint16_t wbe = 0xFFFF) {
    uint64_t code = 1ull | (static_cast<uint64_t>(bank & 0x7u) << 1) |
                    (static_cast<uint64_t>(addr & 0xFFu) << 4) |
                    (static_cast<uint64_t>(wbe & 0xFFFFu) << 12);
    code |= static_cast<uint64_t>(smc & 0x1Fu) << 28;
    return code;
}

uint32_t rtovr_code(uint8_t source_id) {
    return 1u | (static_cast<uint32_t>(source_id & 0x3Fu) << 1);
}

uint16_t intadd_code(bool is_sub, bool update_st, uint8_t smc) {
    iq::IntAddCru cru{};
    cru.para_inst_valid = true;
    cru.para_precision_s0 = 3;
    cru.para_precision_s1 = 3;
    cru.para_precision_s2 = 3;
    cru.para_sign_s0 = true;
    cru.para_sign_s1 = true;
    cru.para_sign_s2 = true;
    cru.para_sign_d = true;
    cru.para_update_st = update_st;
    cru.para_is_sub = is_sub;
    return static_cast<uint16_t>(iq::IntAddIq::pack_cru_intadd(cru) |
                                 (static_cast<uint16_t>(smc & 0x1F) << 13));
}

uint8_t fpmul_code(uint8_t smc) {
    iq::FpMulCru cru{};
    cru.para_inst_valid = true;
    cru.para_src_precision_is_32b = true;
    cru.para_dst_precision_is_32b = true;
    return static_cast<uint8_t>(iq::FpMulIq::pack_cru_fpmul(cru) |
                                static_cast<uint8_t>((smc & 0x1F) << 3));
}

uint16_t fpadd_code(bool is_sub, bool update_st, uint8_t smc) {
    iq::FpAddCru cru{};
    cru.para_inst_valid = true;
    cru.para_src_precision_32b = true;
    cru.para_dst_precision_32b = true;
    cru.para_update_status = update_st;
    cru.para_is_sub = is_sub;
    return static_cast<uint16_t>(iq::FpAddIq::pack_cru_fpadd(cru) |
                                 (static_cast<uint16_t>(smc & 0x1F) << 5));
}

uint16_t inttofp_code(uint8_t smc) {
    iq::IntToFpCru cru{};
    cru.para_inst_valid = true;
    cru.para_src_is_32b = true;
    cru.para_dst_is_32b = true;
    cru.para_src_signed = true;
    return static_cast<uint16_t>(iq::IntToFpIq::pack_cru_inttofp(cru) |
                                 (static_cast<uint16_t>(smc & 0x1F) << 6));
}

uint16_t fptoint_code(uint8_t smc) {
    iq::FpToIntCru cru{};
    cru.para_inst_valid = true;
    cru.para_src_is_32b = true;
    cru.para_dst_is_32b = true;
    return static_cast<uint16_t>(iq::FpToIntIq::pack_cru_fptoint(cru) |
                                 (static_cast<uint16_t>(smc & 0x1F) << 5));
}

uint16_t logic_code(iq::LogicalOp op, uint8_t smc) {
    iq::LogicalCru cru{};
    cru.para_inst_valid = true;
    cru.para_precision_32b = true;
    cru.para_opcode = op;
    return static_cast<uint16_t>(iq::LogicalIq::pack_cru_logical(cru) |
                                 (static_cast<uint16_t>(smc & 0x1F) << 6));
}

uint64_t serialize_header_41(uint8_t vld, uint8_t jump, uint8_t dly) {
    uint64_t bits = 0;
    uint32_t pos = 0;
    pos += 6;  // inc_embed
    pos += 4;  // inc
    bits |= static_cast<uint64_t>(dly & 0x7u) << pos; pos += 3;
    pos += 7;  // cond
    pos += 10; // loops
    pos += 6;  // embed_end
    pos += 3;  // embed
    bits |= static_cast<uint64_t>(jump & 1u) << pos; pos += 1;
    bits |= static_cast<uint64_t>(vld & 1u) << pos;
    return bits;
}

std::vector<uint8_t> pack_instruction(uint8_t vld, uint8_t jump, uint8_t dly,
                                      uint64_t code, uint32_t code_bits) {
    const uint64_t header = serialize_header_41(vld, jump, dly);
    const uint32_t total_bits = 41 + code_bits;
    const uint32_t num_bytes = (total_bits + 7) / 8;
    const unsigned __int128 mask = (code_bits >= 64) ? ~static_cast<unsigned __int128>(0) :
        ((static_cast<unsigned __int128>(1) << code_bits) - 1);
    const unsigned __int128 inst = (static_cast<unsigned __int128>(header) << code_bits) |
                                   (static_cast<unsigned __int128>(code) & mask);
    std::vector<uint8_t> bytes;
    for (int i = static_cast<int>(num_bytes) - 1; i >= 0; --i) {
        bytes.push_back(static_cast<uint8_t>((inst >> (i * 8)) & 0xFF));
    }
    return bytes;
}

std::vector<uint8_t> sched_to_iq_bytes(const std::vector<RemainderScalarInstructions::SchedEntry>& sched,
                                       uint32_t code_bits) {
    std::vector<uint8_t> data;
    int32_t prev_cycle = -1;
    for (const auto& entry : sched) {
        int32_t dly = (prev_cycle < 0) ? static_cast<int32_t>(entry.abs_cycle)
                                      : static_cast<int32_t>(entry.abs_cycle) - prev_cycle - 1;
        while (dly > static_cast<int32_t>(kMaxDlyField)) {
            auto nop = pack_instruction(1, 0, kMaxDlyField, 0, code_bits);
            data.insert(data.end(), nop.begin(), nop.end());
            dly -= 8;
        }
        auto inst = pack_instruction(1, 0, static_cast<uint8_t>(std::max<int32_t>(0, dly)), entry.code, code_bits);
        data.insert(data.end(), inst.begin(), inst.end());
        prev_cycle = static_cast<int32_t>(entry.abs_cycle);
    }
    auto end_inst = pack_instruction(1, 1, 0, 0, code_bits);
    data.insert(data.end(), end_inst.begin(), end_inst.end());
    return data;
}

void add_sched_file(std::vector<IqInstructionFile>& files,
                    const std::vector<RemainderScalarInstructions::SchedEntry>& sched,
                    uint32_t iq_id,
                    const std::string& iq_name,
                    uint32_t code_bits) {
    if (sched.empty()) return;
    IqInstructionFile file;
    file.para_iq_id = iq_id;
    file.para_iq_name = iq_name;
    file.para_width_bits = 41 + code_bits;
    file.para_data = sched_to_iq_bytes(sched, code_bits);
    files.push_back(file);
}

void route(std::vector<RemainderScalarInstructions::SchedEntry>& sched, uint32_t cycle, uint8_t source) {
    sched.push_back({cycle, rtovr_code(source)});
}

} // namespace

std::string RemainderScalarOp::get_name() const {
    return "RemainderScalar";
}

OpInstructionsBase* RemainderScalarOp::compile(const app::AppOperatorRequest& req) {
    if (req.para_layout_mode == "bank_conflict") {
        throw std::runtime_error("RemainderScalar only supports default layout for now");
    }

    RemainderScalarParams p;
    p.para_n = req.para_dimensions.para_n;
    p.para_c = req.para_dimensions.para_c;
    p.para_h = req.para_dimensions.para_h;
    p.para_w = req.para_dimensions.para_w;

    auto input_it = req.para_inputs.find("input_tensor");
    if (input_it == req.para_inputs.end()) input_it = req.para_inputs.find("input");
    if (input_it != req.para_inputs.end()) p.para_dtype = to_dtype(input_it->second.para_dtype);

    p.para_input_bank = static_cast<uint8_t>(get_i32_param(req, "input_bank", 0));
    p.para_input_addr = static_cast<uint8_t>(get_i32_param(req, "input_addr", 0));
    p.para_scalar_bank = static_cast<uint8_t>(get_i32_param(req, "other_bank", 1));
    p.para_scalar_addr = static_cast<uint8_t>(get_i32_param(req, "other_addr", 0));
    p.para_const_bank = static_cast<uint8_t>(get_i32_param(req, "const_bank", get_i32_param(req, "alpha_bank", 3)));
    p.para_x0_addr = static_cast<uint8_t>(get_i32_param(req, "x0_addr", 0));
    p.para_const2_addr = static_cast<uint8_t>(get_i32_param(req, "const2_addr", 1));
    p.para_one_addr = static_cast<uint8_t>(get_i32_param(req, "one_addr", 2));
    p.para_zero_addr = static_cast<uint8_t>(get_i32_param(req, "zero_addr", 3));
    p.para_shift_base_addr = static_cast<uint8_t>(get_i32_param(req, "shift_base_addr", 4));
    p.para_output_bank = static_cast<uint8_t>(get_i32_param(req, "output_bank", 2));
    p.para_output_addr = static_cast<uint8_t>(get_i32_param(req, "output_addr", 0));
    p.para_scalar_value = get_scalar(req);
    p.para_newton_iters = static_cast<uint32_t>(get_i32_param(req, "newton_iters", 4));
    if (p.para_newton_iters == 0) p.para_newton_iters = 4;
    p.para_smc_id_en = static_cast<uint8_t>(get_i32_param(req, "smc_id_en", 7));

    if (p.para_scalar_value == 0) {
        throw std::runtime_error("RemainderScalar scalar must not be zero");
    }
    if (p.para_scalar_value == std::numeric_limits<int32_t>::min()) {
        throw std::runtime_error("RemainderScalar does not support INT32_MIN scalar in shift-add product path");
    }

    RemainderScalarInstructions* insts = new RemainderScalarInstructions();
    *insts = compile(p);
    return insts;
}

RemainderScalarInstructions RemainderScalarOp::compile(const RemainderScalarParams& p) {
    RemainderScalarInstructions r;
    r.para_dtype = p.para_dtype;
    r.para_smc_id_en = p.para_smc_id_en;
    const uint8_t smc = p.para_smc_id_en & 0x1F;
    const uint32_t total_elems = p.para_n * p.para_c * p.para_h * p.para_w;
    const uint32_t total_steps = (total_elems + p.get_elems_per_word() - 1) / p.get_elems_per_word();
    if (total_steps == 0) throw std::runtime_error("RemainderScalar: total_steps is zero");
    r.para_total_steps = total_steps;
    r.para_total_elements = total_elems;

    const uint32_t scalar_abs = static_cast<uint32_t>(p.para_scalar_value < 0 ? -static_cast<int64_t>(p.para_scalar_value) : p.para_scalar_value);
    const bool scalar_neg = p.para_scalar_value < 0;

    const uint16_t add_code = intadd_code(false, false, smc);
    const uint16_t sub_code = intadd_code(true, false, smc);
    const uint16_t fp_sub_st_code = fpadd_code(true, true, smc);
    const uint16_t fp_sub_code = fpadd_code(true, false, smc);
    const uint8_t mul_code = fpmul_code(smc);
    const uint16_t itof_code = inttofp_code(smc);
    const uint16_t ftoi_code = fptoint_code(smc);
    const uint16_t select_less_code = logic_code(iq::LogicalOp::SELECT_LESS, smc);
    const uint16_t shift_code = logic_code(iq::LogicalOp::ARITH_LEFT_SHIFT, smc);

    uint32_t scalar_pop = 0;
    for (uint32_t bit = 0; bit < 31; ++bit) {
        if ((scalar_abs >> bit) & 1u) ++scalar_pop;
    }
    const uint32_t newton_iter_cycles = 24;
    const uint32_t quotient_cycles = 36;
    const uint32_t product_cycles = std::max<uint32_t>(1, scalar_pop) * 10 + (scalar_neg ? 6 : 0) + 12;
    const uint32_t step_cycles = p.para_newton_iters * newton_iter_cycles + quotient_cycles + product_cycles;

    for (uint32_t step = 0; step < total_steps; ++step) {
        const uint32_t c = step * step_cycles;

        // Newton-Raphson reciprocal: x_{n+1} = x_n * (2 - scalar * x_n).
        for (uint32_t it = 0; it < p.para_newton_iters; ++it) {
            const uint32_t B = c + it * newton_iter_cycles;
            const bool first_iter = (it == 0);
            const uint8_t x_bank = first_iter ? p.para_const_bank : p.para_output_bank;
            const uint32_t x_addr = first_iter ? p.para_x0_addr : (p.para_output_addr + step);

            r.sched_ur_r1.push_back({B + 0, ur_read_code_smc(p.para_scalar_bank, p.para_scalar_addr, smc)});
            r.sched_ur_r2.push_back({B + 0, ur_read_code_smc(x_bank, x_addr, smc)});
            route(r.sched_rtovr_10, B + 1, RTOVR_IN_UR_RDATA_1);
            r.sched_inttofp.push_back({B + 2, itof_code});

            route(r.sched_rtovr_3, B + 4, RTOVR_IN_DR_INTTOFP_D);
            route(r.sched_rtovr_4, B + 4, RTOVR_IN_UR_RDATA_2);
            r.sched_fpmul.push_back({B + 5, mul_code});

            r.sched_ur_r0.push_back({B + 8, ur_read_code_smc(p.para_const_bank, p.para_const2_addr, smc)});
            route(r.sched_rtovr_5, B + 10, RTOVR_IN_UR_RDATA_0);
            route(r.sched_rtovr_6, B + 10, RTOVR_IN_DR_FPMUL_D);
            r.sched_fpadd.push_back({B + 11, fp_sub_code});

            route(r.sched_rtovr_3, B + 15, RTOVR_IN_UR_RDATA_2);
            route(r.sched_rtovr_4, B + 15, RTOVR_IN_DR_FPADD_D);
            r.sched_fpmul.push_back({B + 16, mul_code});

            route(r.sched_rtovr_35, B + 20, RTOVR_IN_DR_FPMUL_D);
            r.sched_ur_w0.push_back({B + 21, ur_write_code_smc(p.para_output_bank, p.para_output_addr + step, smc)});
        }

        const uint32_t Q = c + p.para_newton_iters * newton_iter_cycles;

        // q_fp = float(x) * reciprocal_from_newton.
        r.sched_ur_r0.push_back({Q + 0, ur_read_code_smc(p.para_input_bank, p.para_input_addr + step, smc)});
        r.sched_ur_r1.push_back({Q + 0, ur_read_code_smc(p.para_output_bank, p.para_output_addr + step, smc)});
        route(r.sched_rtovr_10, Q + 1, RTOVR_IN_UR_RDATA_0);
        r.sched_inttofp.push_back({Q + 2, itof_code});
        route(r.sched_rtovr_3, Q + 4, RTOVR_IN_DR_INTTOFP_D);
        route(r.sched_rtovr_4, Q + 4, RTOVR_IN_UR_RDATA_1);
        r.sched_fpmul.push_back({Q + 5, mul_code});

        // q_trunc = FPtoINT(q_fp); q_int_fp = INTtoFP(q_trunc).
        route(r.sched_rtovr_9, Q + 11, RTOVR_IN_DR_FPMUL_D);
        r.sched_fptoint.push_back({Q + 12, ftoi_code});
        route(r.sched_rtovr_10, Q + 15, RTOVR_IN_DR_FPTOINT_D);
        r.sched_inttofp.push_back({Q + 16, itof_code});

        // status = compare(q_fp - q_int_fp), q_minus_one = q_trunc - 1.
        route(r.sched_rtovr_5, Q + 18, RTOVR_IN_DR_FPMUL_D);
        route(r.sched_rtovr_6, Q + 18, RTOVR_IN_DR_INTTOFP_D);
        r.sched_fpadd.push_back({Q + 19, fp_sub_st_code});
        r.sched_ur_r2.push_back({Q + 18, ur_read_code_smc(p.para_const_bank, p.para_one_addr, smc)});
        route(r.sched_rtovr_0, Q + 19, RTOVR_IN_DR_FPTOINT_D);
        route(r.sched_rtovr_1, Q + 19, RTOVR_IN_UR_RDATA_2);
        r.sched_intadd.push_back({Q + 20, sub_code});

        // q_floor = (q_fp < q_trunc_fp) ? q_trunc - 1 : q_trunc.
        route(r.sched_rtovr_11, Q + 25, RTOVR_IN_DR_INTADD_D0);
        route(r.sched_rtovr_12, Q + 25, RTOVR_IN_DR_FPTOINT_D);
        route(r.sched_rtovr_13, Q + 25, RTOVR_IN_DR_FPADD_ST);
        r.sched_logic.push_back({Q + 26, select_less_code});
        route(r.sched_rtovr_35, Q + 29, RTOVR_IN_DR_LOGIC_D);
        r.sched_ur_w0.push_back({Q + 30, ur_write_code_smc(p.para_output_bank, p.para_output_addr + step, smc)});

        // product = q_floor * scalar, implemented as shift-add by scalar constant bits.
        bool have_acc = false;
        uint32_t pc = Q + 36;
        for (uint32_t bit = 0; bit < 31; ++bit) {
            if (((scalar_abs >> bit) & 1u) == 0) continue;
            r.sched_ur_r1.push_back({pc + 0, ur_read_code_smc(p.para_output_bank, p.para_output_addr + step, smc)});
            r.sched_ur_r2.push_back({pc + 0, ur_read_code_smc(p.para_const_bank, p.para_shift_base_addr + bit, smc)});
            route(r.sched_rtovr_11, pc + 1, RTOVR_IN_UR_RDATA_1);
            route(r.sched_rtovr_12, pc + 1, RTOVR_IN_UR_RDATA_2);
            r.sched_logic.push_back({pc + 2, shift_code});
            if (!have_acc) {
                r.sched_ur_r2.push_back({pc + 4, ur_read_code_smc(p.para_const_bank, p.para_zero_addr, smc)});
                route(r.sched_rtovr_0, pc + 5, RTOVR_IN_DR_LOGIC_D);
                route(r.sched_rtovr_1, pc + 5, RTOVR_IN_UR_RDATA_2);
            } else {
                route(r.sched_rtovr_0, pc + 5, RTOVR_IN_DR_INTADD_D0);
                route(r.sched_rtovr_1, pc + 5, RTOVR_IN_DR_LOGIC_D);
            }
            r.sched_intadd.push_back({pc + 6, add_code});
            have_acc = true;
            pc += 10;
        }
        if (!have_acc) {
            r.sched_ur_r2.push_back({pc + 0, ur_read_code_smc(p.para_const_bank, p.para_zero_addr, smc)});
            route(r.sched_rtovr_0, pc + 1, RTOVR_IN_UR_RDATA_2);
            route(r.sched_rtovr_1, pc + 1, RTOVR_IN_UR_RDATA_2);
            r.sched_intadd.push_back({pc + 2, add_code});
            pc += 6;
        }
        if (scalar_neg) {
            r.sched_ur_r2.push_back({pc + 0, ur_read_code_smc(p.para_const_bank, p.para_zero_addr, smc)});
            route(r.sched_rtovr_0, pc + 1, RTOVR_IN_UR_RDATA_2);
            route(r.sched_rtovr_1, pc + 1, RTOVR_IN_DR_INTADD_D0);
            r.sched_intadd.push_back({pc + 2, sub_code});
            pc += 6;
        }

        // out = x - product.
        r.sched_ur_r0.push_back({pc + 0, ur_read_code_smc(p.para_input_bank, p.para_input_addr + step, smc)});
        route(r.sched_rtovr_0, pc + 1, RTOVR_IN_UR_RDATA_0);
        route(r.sched_rtovr_1, pc + 1, RTOVR_IN_DR_INTADD_D0);
        r.sched_intadd.push_back({pc + 2, sub_code});
        route(r.sched_rtovr_35, pc + 5, RTOVR_IN_DR_INTADD_D0);
        r.sched_ur_w0.push_back({pc + 6, ur_write_code_smc(p.para_output_bank, p.para_output_addr + step, smc)});
        r.para_total_cycles = std::max(r.para_total_cycles, pc + 10);
    }

    std::cout << "[RemainderScalar] Bank config:\n"
              << "  Input:  bank=" << static_cast<int>(p.para_input_bank) << ", addr=" << static_cast<int>(p.para_input_addr) << "\n"
              << "  Scalar: bank=" << static_cast<int>(p.para_scalar_bank) << ", addr=" << static_cast<int>(p.para_scalar_addr) << " (raw scalar)\n"
              << "  Const:  bank=" << static_cast<int>(p.para_const_bank) << " (x0/2.0/one/zero/shift constants)\n"
              << "  Output: bank=" << static_cast<int>(p.para_output_bank) << ", addr=" << static_cast<int>(p.para_output_addr) << "\n"
              << "  Path:   Newton reciprocal + FP quotient + floor fix + shift-add product + INTADD subtract, scalar="
              << p.para_scalar_value << ", smc_id_en=" << static_cast<int>(p.para_smc_id_en) << "\n";
    return r;
}

std::vector<IqInstructionFile> RemainderScalarInstructions::get_iq_files() const {
    std::vector<IqInstructionFile> files;
    add_sched_file(files, sched_ur_r0, iq::IQ_ID_UR_RDATA_0, iq::IQ_NAME_UR_RDATA_0, 17);
    add_sched_file(files, sched_ur_r1, iq::IQ_ID_UR_RDATA_1, iq::IQ_NAME_UR_RDATA_1, 17);
    add_sched_file(files, sched_ur_r2, iq::IQ_ID_UR_RDATA_2, iq::IQ_NAME_UR_RDATA_2, 17);
    add_sched_file(files, sched_ur_w0, iq::IQ_ID_UR_WDATA_0, iq::IQ_NAME_UR_WDATA_0, 33);
    add_sched_file(files, sched_rtovr_0, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_INTADD_S0, "rtovr-0", 7);
    add_sched_file(files, sched_rtovr_1, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_INTADD_S1, "rtovr-1", 7);
    add_sched_file(files, sched_rtovr_3, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_FPMUL_S0, "rtovr-3", 7);
    add_sched_file(files, sched_rtovr_4, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_FPMUL_S1, "rtovr-4", 7);
    add_sched_file(files, sched_rtovr_5, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_FPADD_S0, "rtovr-5", 7);
    add_sched_file(files, sched_rtovr_6, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_FPADD_S1, "rtovr-6", 7);
    add_sched_file(files, sched_rtovr_9, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_FPTOINT_S, "rtovr-9", 7);
    add_sched_file(files, sched_rtovr_10, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_INTTOFP_S, "rtovr-10", 7);
    add_sched_file(files, sched_rtovr_11, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_LOGIC_S0, "rtovr-11", 7);
    add_sched_file(files, sched_rtovr_12, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_LOGIC_S1, "rtovr-12", 7);
    add_sched_file(files, sched_rtovr_13, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_DVR_LOGIC_ST, "rtovr-13", 7);
    add_sched_file(files, sched_rtovr_35, iq::IQ_ID_RTOVR_BASE + RTOVR_OUT_UVR_WDATA_0, "rtovr-35", 7);
    add_sched_file(files, sched_intadd, iq::IQ_ID_INTADD, iq::IQ_NAME_INTADD, 18);
    add_sched_file(files, sched_fpmul, iq::IQ_ID_FPMUL, iq::IQ_NAME_FPMUL, 8);
    add_sched_file(files, sched_fpadd, iq::IQ_ID_FPADD, iq::IQ_NAME_FPADD, 10);
    add_sched_file(files, sched_fptoint, iq::IQ_ID_FP_TO_INT, iq::IQ_NAME_FP_TO_INT, 10);
    add_sched_file(files, sched_inttofp, iq::IQ_ID_INT_TO_FP, iq::IQ_NAME_INT_TO_FP, 11);
    add_sched_file(files, sched_logic, iq::IQ_ID_LOGIC, iq::IQ_NAME_LOGIC, 11);
    return files;
}

bool RemainderScalarOp::calculate_golden(const app::AppOperatorRequest& req,
                                         std::vector<uint8_t>& golden_bin) {
    golden_bin.clear();
    auto it = req.para_inputs.find("input_tensor");
    if (it == req.para_inputs.end()) it = req.para_inputs.find("input");
    if (it == req.para_inputs.end() || it->second.para_dtype != app::AppDataType::INT32) return false;
    const int32_t scalar = get_scalar(req);
    if (scalar == 0) return false;
    for (int32_t x : it->second.para_data_i32) {
        const int64_t q = floor_div_i64(x, scalar);
        const int64_t r = static_cast<int64_t>(x) - q * static_cast<int64_t>(scalar);
        push_u32(golden_bin, static_cast<uint32_t>(static_cast<int32_t>(r)));
    }
    return true;
}

bool RemainderScalarOp::generate_data(const app::AppOperatorRequest& req,
                                      std::vector<uint8_t>& input_bin,
                                      std::vector<uint8_t>& other_bin,
                                      std::vector<uint8_t>& alpha_bin) {
    input_bin.clear();
    other_bin.clear();
    alpha_bin.clear();
    auto it = req.para_inputs.find("input_tensor");
    if (it == req.para_inputs.end()) it = req.para_inputs.find("input");
    if (it == req.para_inputs.end() || it->second.para_dtype != app::AppDataType::INT32) return false;
    const int32_t scalar = get_scalar(req);
    if (scalar == 0) return false;
    for (int32_t x : it->second.para_data_i32) push_u32(input_bin, static_cast<uint32_t>(x));
    for (int lane = 0; lane < 4; ++lane) push_u32(other_bin, static_cast<uint32_t>(scalar));

    const float x0 = initial_recip_guess(scalar);
    for (int lane = 0; lane < 4; ++lane) push_f32(alpha_bin, x0);
    for (int lane = 0; lane < 4; ++lane) push_f32(alpha_bin, 2.0f);
    for (int lane = 0; lane < 4; ++lane) push_u32(alpha_bin, 1u);
    for (int lane = 0; lane < 4; ++lane) push_u32(alpha_bin, 0u);
    for (uint32_t bit = 0; bit < 31; ++bit) {
        for (int lane = 0; lane < 4; ++lane) push_u32(alpha_bin, bit);
    }
    return true;
}

void RemainderScalarOp::fill_random_request(app::AppOperatorRequest& req,
                                            std::mt19937& rng,
                                            std::uniform_real_distribution<float>& data_dist,
                                            const std::string& dtype_str) {
    (void)dtype_str;
    req.para_operator = "RemainderScalar";
    if (req.para_layout_mode == "bank_conflict") req.para_layout_mode = "default";
    const uint32_t total = req.para_dimensions.para_n * req.para_dimensions.para_c *
                           req.para_dimensions.para_h * req.para_dimensions.para_w;
    app::AppTensorDesc input;
    input.para_name = "input_tensor";
    input.para_dtype = app::AppDataType::INT32;
    input.para_shape = {req.para_dimensions.para_n, req.para_dimensions.para_c,
                        req.para_dimensions.para_h, req.para_dimensions.para_w};
    input.para_data_i32.reserve(total);
    for (uint32_t i = 0; i < total; ++i) {
        input.para_data_i32.push_back(static_cast<int32_t>(std::round(data_dist(rng) * 64.0f)));
    }
    req.para_inputs["input_tensor"] = input;

    std::uniform_int_distribution<int32_t> scalar_dist(-17, 17);
    int32_t scalar_value = 0;
    while (scalar_value == 0) scalar_value = scalar_dist(rng);
    if (scalar_value == 16 || scalar_value == -16) scalar_value = 7;

    app::AppTensorDesc scalar;
    scalar.para_name = "other_scalar";
    scalar.para_dtype = app::AppDataType::INT32;
    scalar.para_shape = {1};
    scalar.para_data_i32.push_back(scalar_value);
    req.para_parameters["other_scalar"] = scalar;

    auto set_i32 = [&](const std::string& name, int v) {
        app::AppTensorDesc d;
        d.para_name = name;
        d.para_dtype = app::AppDataType::INT32;
        d.para_shape = {1};
        d.para_data_i32.push_back(v);
        req.para_parameters[name] = d;
    };
    set_i32("input_bank", 0);
    set_i32("input_addr", 0);
    set_i32("other_bank", 1);
    set_i32("other_addr", 0);
    set_i32("alpha_bank", 3);
    set_i32("const_bank", 3);
    set_i32("alpha_addr", 0);
    set_i32("x0_addr", 0);
    set_i32("const2_addr", 1);
    set_i32("one_addr", 2);
    set_i32("zero_addr", 3);
    set_i32("shift_base_addr", 4);
    set_i32("output_bank", 2);
    set_i32("output_addr", 0);
}

std::vector<uint8_t> RemainderScalarInstructions::serialize() const {
    std::vector<uint8_t> out;
    push_u32(out, para_total_steps);
    push_u32(out, para_total_elements);
    push_u32(out, para_total_cycles);
    return out;
}

REGISTER_OP(RemainderScalarOp);

} // namespace op
} // namespace aiacc



