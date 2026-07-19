#include "to_copy_op.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <map>
#include <cmath>
#include "../../iq/iq_id_defs.h"
#include "../common/op_registry.h"
#include "../../app/fp16_utils.h"

namespace aiacc {
namespace op {

namespace {
constexpr uint8_t kMaxDlyField = 7;

// DLY 超过 7 bit 字段表达范围时，用 dummy 指令（dly=7）补齐
template <typename EmitDummyFn>
uint8_t emit_dummy_for_long_delay(uint32_t& total_dly, EmitDummyFn&& emit_dummy) {
    while (total_dly > kMaxDlyField) {
        emit_dummy();
        total_dly -= 8;
    }
    return static_cast<uint8_t>(total_dly);
}

// UR 读写端口 CODE 提取
inline uint32_t extract_ur_read_port0_code(const iq::UrCru& cru) {
    const uint32_t bank = cru.para_raddr[0].bank & 0x7u;
    const uint32_t addr = cru.para_raddr[0].addr_word & 0xFFu;
    const uint32_t re   = cru.para_re[0] ? 1u : 0u;
    return re | (bank << 1) | (addr << 4);
}

inline uint32_t extract_ur_write_port0_code(const iq::UrCru& cru) {
    const uint32_t bank = cru.para_waddr[0].bank & 0x7u;
    const uint32_t addr = cru.para_waddr[0].addr_word & 0xFFu;
    const uint32_t we   = cru.para_we[0] ? 1u : 0u;
    const uint32_t wbe  = cru.para_wbe[0] & 0xFFFFu;
    return we | (bank << 1) | (addr << 4) | (wbe << 12);
}
} // namespace

// ==========================================
// UR 指令生成
// ==========================================
static void gen_ur_read_inst(const ToCopyParams& params, uint32_t step, iq::UrCru& cru) {
    cru.para_vld = true;
    cru.para_raddr[0].bank      = params.para_input_bank;
    cru.para_raddr[0].addr_word = params.para_input_addr + step;
    cru.para_re[0] = true;
    cru.para_re[1] = false;
    cru.para_re[2] = false;
    cru.para_re[3] = false;
    cru.para_we[0] = false;
    cru.para_we[1] = false;
    cru.para_we[2] = false;
    cru.para_we[3] = false;
}

static void gen_ur_write_inst(const ToCopyParams& params, uint32_t step, iq::UrCru& cru) {
    cru.para_vld = true;
    cru.para_re[0] = false;
    cru.para_re[1] = false;
    cru.para_re[2] = false;
    cru.para_re[3] = false;
    cru.para_waddr[0].bank      = params.para_output_bank;
    cru.para_waddr[0].addr_word = params.para_output_addr + step;
    cru.para_we[0]  = true;
    cru.para_wbe[0] = 0xFFFF;
    cru.para_we[1] = false;
    cru.para_we[2] = false;
    cru.para_we[3] = false;
}

// ==========================================
// 算子注册与接口
// ==========================================
std::string ToCopyOp::get_name() const { return "_to_copy"; }

OpInstructionsBase* ToCopyOp::compile(const app::AppOperatorRequest& req) {
    ToCopyParams params;
    params.para_n = req.para_dimensions.para_n;
    params.para_c = req.para_dimensions.para_c;
    params.para_h = req.para_dimensions.para_h;
    params.para_w = req.para_dimensions.para_w;
    if (params.para_n == 0 || params.para_c == 0 || params.para_h == 0 || params.para_w == 0) {
        throw std::runtime_error("ToCopy: invalid dimensions (0 not allowed)");
    }

    // 默认输入数据类型
    params.para_in_dtype = app::AppDataType::FLOAT32;
    {
        auto it = req.para_inputs.find("input_tensor");
        if (it != req.para_inputs.end()) {
            params.para_in_dtype = it->second.para_dtype;
        }
    }

    // 默认输出数据类型与输入相同，如果指定了 out_dtype 参数则覆盖
    params.para_out_dtype = params.para_in_dtype;
    {
        auto it = req.para_parameters.find("out_dtype");
        if (it != req.para_parameters.end() && !it->second.para_data_i32.empty()) {
            params.para_out_dtype = static_cast<app::AppDataType>(it->second.para_data_i32[0]);
        }
    }

    auto get_param = [&](const std::string& key, int def) -> int {
        auto it = req.para_parameters.find(key);
        if (it != req.para_parameters.end() && !it->second.para_data_i32.empty())
            return it->second.para_data_i32[0];
        return def;
    };
    params.para_input_bank  = static_cast<uint8_t>(get_param("input_bank",  0));
    params.para_input_addr  = static_cast<uint8_t>(get_param("input_addr",  0));
    params.para_output_bank = static_cast<uint8_t>(get_param("output_bank", 1));
    params.para_output_addr = static_cast<uint8_t>(get_param("output_addr", 0));

    ToCopyInstructions* insts = new ToCopyInstructions();
    *insts = compile(params);
    return insts;
}

ToCopyInstructions ToCopyOp::compile(const ToCopyParams& params) {
    ToCopyInstructions result;
    result.para_in_dtype    = params.para_in_dtype;
    result.para_out_dtype   = params.para_out_dtype;
    result.para_input_bank  = params.para_input_bank;
    result.para_output_bank = params.para_output_bank;
    result.para_input_addr  = params.para_input_addr;
    result.para_output_addr = params.para_output_addr;
    result.para_bank_mode   = params.is_same_bank() ? ToCopyBankMode::SAME_BANK : ToCopyBankMode::DIFF_BANK;

    const uint32_t total_elems = params.para_n * params.para_c * params.para_h * params.para_w;
    result.para_total_elements = total_elems;

    // 根据转换类型确定计算单元及控制字打包
    bool in_is_fp = (params.para_in_dtype == app::AppDataType::FLOAT32 || params.para_in_dtype == app::AppDataType::FLOAT16);
    bool out_is_fp = (params.para_out_dtype == app::AppDataType::FLOAT32 || params.para_out_dtype == app::AppDataType::FLOAT16);

    uint32_t elems_per_step = 4; // 默认每拍处理4通道 (32bit)

    if (params.para_in_dtype == params.para_out_dtype) {
        // 1. 同类型拷贝：使用 LOGIC COPY (IQ 11)
        result.para_compute_iq_id = iq::IQ_ID_LOGIC;
        result.para_compute_iq_name = iq::IQ_NAME_LOGIC;
        result.para_code_bits = 6;

        iq::LogicalCru cru;
        cru.para_inst_valid = true;
        cru.para_precision_32b = (params.para_in_dtype == app::AppDataType::FLOAT16) ? false : true;
        cru.para_opcode = iq::LogicalOp::COPY;
        result.para_compute_inst = iq::LogicalIq::pack_cru_logical(cru);

        if (params.para_in_dtype == app::AppDataType::FLOAT16) {
            elems_per_step = 8; // 16bit 模式下 128bit word 包含 8 个元素
        }
    }
    else if (in_is_fp && out_is_fp) {
        // 2. 浮点与浮点转换（FP32 <-> FP16）：使用 FPtoFP (IQ 7)
        result.para_compute_iq_id = iq::IQ_ID_FP_TO_FP;
        result.para_compute_iq_name = "fptofp";
        result.para_code_bits = 5;

        iq::FpToFpCru cru;
        cru.para_inst_valid = true;
        cru.para_src_is_32b = (params.para_in_dtype == app::AppDataType::FLOAT32);
        cru.para_dst_is_32b = (params.para_out_dtype == app::AppDataType::FLOAT32);
        cru.para_src_high = false;
        cru.para_dst_high = false;
        result.para_compute_inst = iq::FpToFpIq::pack_cru_fptofp(cru);
    }
    else if (in_is_fp && !out_is_fp) {
        // 3. 浮点转整数（FP -> INT32）：使用 FPtoInt (IQ 9)
        result.para_compute_iq_id = iq::IQ_ID_FP_TO_INT;
        result.para_compute_iq_name = "fptoint";
        result.para_code_bits = 5;

        iq::FpToIntCru cru;
        cru.para_inst_valid = true;
        cru.para_src_is_32b = (params.para_in_dtype == app::AppDataType::FLOAT32);
        cru.para_dst_is_32b = true; // 目标为 INT32
        cru.para_src_high = false;
        cru.para_dst_high = false;
        result.para_compute_inst = iq::FpToIntIq::pack_cru_fptoint(cru);
    }
    else if (!in_is_fp && out_is_fp) {
        // 4. 整数转浮点（INT32 -> FP）：使用 INTtoFP (IQ 10)
        result.para_compute_iq_id = iq::IQ_ID_INT_TO_FP;
        result.para_compute_iq_name = "inttofp";
        result.para_code_bits = 6;

        iq::IntToFpCru cru;
        cru.para_inst_valid = true;
        cru.para_src_is_32b = true; // 输入为 INT32
        cru.para_dst_is_32b = (params.para_out_dtype == app::AppDataType::FLOAT32);
        cru.para_src_signed = true;
        cru.para_src_high = false;
        cru.para_dst_high = false;
        result.para_compute_inst = iq::IntToFpIq::pack_cru_inttofp(cru);
    }
    else {
        // 5. 整数转整数（INT32 <-> INT16，占位支持）：使用 INTtoInt (IQ 8)
        result.para_compute_iq_id = iq::IQ_ID_INT_TO_INT;
        result.para_compute_iq_name = "inttoint";
        result.para_code_bits = 7;

        iq::IntToIntCru cru;
        cru.para_inst_valid = true;
        cru.para_src_is_32b = true;
        cru.para_dst_is_32b = true;
        cru.para_src_signed = true;
        cru.para_dst_signed = true;
        cru.para_src_high = false;
        cru.para_dst_high = false;
        result.para_compute_inst = iq::IntToIntIq::pack_cru_inttoint(cru);
    }

    const uint32_t total_steps = (total_elems + elems_per_step - 1) / elems_per_step;
    result.para_total_steps = total_steps;

    // 路由映射：输入读取路由
    // UR_RDATA_0 (30) -> 计算单元 S 端
    // - LOGIC: 11
    // - FPtoFP: 7
    // - FPtoInt: 9
    // - INTtoFP: 10
    // - INTtoInt: 8
    uint8_t dvr_s_port = 11;
    if (result.para_compute_iq_id == iq::IQ_ID_FP_TO_FP) dvr_s_port = 7;
    else if (result.para_compute_iq_id == iq::IQ_ID_FP_TO_INT) dvr_s_port = 9;
    else if (result.para_compute_iq_id == iq::IQ_ID_INT_TO_FP) dvr_s_port = 10;
    else if (result.para_compute_iq_id == iq::IQ_ID_INT_TO_INT) dvr_s_port = 8;

    result.para_rtovr_routes.push_back({dvr_s_port, 30});

    // 路由映射：结果写回路由
    // 计算单元 D 端 -> UVR_WDATA_0 (35)
    // - LOGIC: 10
    // - FPtoFP: 6
    // - FPtoInt: 8
    // - INTtoFP: 9
    // - INTtoInt: 7
    uint8_t dr_d_port = 10;
    if (result.para_compute_iq_id == iq::IQ_ID_FP_TO_FP) dr_d_port = 6;
    else if (result.para_compute_iq_id == iq::IQ_ID_FP_TO_INT) dr_d_port = 8;
    else if (result.para_compute_iq_id == iq::IQ_ID_INT_TO_FP) dr_d_port = 9;
    else if (result.para_compute_iq_id == iq::IQ_ID_INT_TO_INT) dr_d_port = 7;

    result.para_rtovr_routes.push_back({35, dr_d_port});

    // 组装单个 UR 读写指令
    iq::UrCru ur_read_cru;
    gen_ur_read_inst(params, 0, ur_read_cru);
    result.para_ur_read_crus.push_back(ur_read_cru);

    iq::UrCru ur_write_cru;
    gen_ur_write_inst(params, 0, ur_write_cru);
    result.para_ur_write_crus.push_back(ur_write_cru);

    // 周期统计，流水线延迟为 1 周期，路由延迟 1 周期
    constexpr uint32_t PIPELINE_LATENCY = 2; 
    constexpr uint32_t BATCH = 4;
    if (result.para_bank_mode == ToCopyBankMode::DIFF_BANK) {
        result.para_total_cycles = PIPELINE_LATENCY + (total_steps - 1) * BATCH;
    } else {
        result.para_total_cycles = total_steps * BATCH + PIPELINE_LATENCY + total_steps * BATCH;
    }

    return result;
}

// ==========================================
// get_iq_files：组装指令流数据
// ==========================================
std::vector<IqInstructionFile> ToCopyInstructions::get_iq_files() const {
    std::vector<IqInstructionFile> files;

    constexpr uint32_t BITS_EMBED = 3, BITS_EMBED_END = 6, BITS_LOOPS = 10;
    constexpr uint32_t BITS_COND = 7, BITS_DLY = 3, BITS_INC = 4, BITS_INC_EMBED = 6;
    constexpr uint32_t MASK_EMBED     = (1u << BITS_EMBED)     - 1;
    constexpr uint32_t MASK_EMBED_END = (1u << BITS_EMBED_END) - 1;
    constexpr uint32_t MASK_LOOPS     = (1u << BITS_LOOPS)     - 1;
    constexpr uint32_t MASK_COND      = (1u << BITS_COND)      - 1;
    constexpr uint32_t MASK_DLY       = (1u << BITS_DLY)       - 1;
    constexpr uint32_t MASK_INC       = (1u << BITS_INC)       - 1;
    constexpr uint32_t MASK_INC_EMBED = (1u << BITS_INC_EMBED) - 1;

    auto serialize_header_bits = [&](uint8_t vld, uint8_t jump, uint8_t embed,
        uint16_t loops, uint8_t cond, uint8_t dly, uint8_t inc,
        uint8_t embed_end_bits, uint8_t inc_embed_bits) -> uint64_t {
        uint64_t bits = 0; uint32_t pos = 0;
        uint8_t embed_end = (embed == 0) ? (embed_end_bits & MASK_EMBED_END) : 0u;
        uint8_t inc_embed = (embed == 0) ? (inc_embed_bits & MASK_INC_EMBED) : 0u;
        bits |= (uint64_t)(inc_embed & MASK_INC_EMBED) << pos; pos += BITS_INC_EMBED;
        bits |= (uint64_t)(inc       & MASK_INC)       << pos; pos += BITS_INC;
        bits |= (uint64_t)(dly       & MASK_DLY)       << pos; pos += BITS_DLY;
        bits |= (uint64_t)(cond      & MASK_COND)      << pos; pos += BITS_COND;
        bits |= (uint64_t)(loops     & MASK_LOOPS)     << pos; pos += BITS_LOOPS;
        bits |= (uint64_t)(embed_end & MASK_EMBED_END) << pos; pos += BITS_EMBED_END;
        bits |= (uint64_t)(embed     & MASK_EMBED)     << pos; pos += BITS_EMBED;
        bits |= (uint64_t)(jump      & 1u)             << pos; pos += 1;
        bits |= (uint64_t)(vld       & 1u)             << pos;
        return bits;
    };

    auto serialize_header = [&](uint8_t vld, uint8_t jump, uint8_t embed,
        uint16_t loops, uint8_t cond, uint8_t dly, uint8_t inc,
        uint8_t embed_end_bits, uint8_t inc_embed_bits) -> std::vector<uint8_t> {
        uint64_t bits = serialize_header_bits(vld, jump, embed, loops, cond, dly,
                                              inc, embed_end_bits, inc_embed_bits);
        std::vector<uint8_t> bytes;
        for (int i = 5; i >= 0; --i)
            bytes.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
        return bytes;
    };

    auto serialize_code = [](uint64_t code, uint16_t code_bits) -> std::vector<uint8_t> {
        std::vector<uint8_t> bytes;
        uint16_t num_bytes = (code_bits + 7) / 8;
        for (int i = num_bytes - 1; i >= 0; --i)
            bytes.push_back(static_cast<uint8_t>((code >> (i * 8)) & 0xFF));
        return bytes;
    };

    auto make_instruction = [&](uint8_t vld, uint8_t jump, uint8_t embed, uint16_t loops,
        uint8_t cond, uint8_t dly, int8_t inc, uint8_t embed_end_bits, uint8_t inc_embed_bits,
        const std::vector<uint8_t>& code_bytes, int code_bits = -1) -> std::vector<uint8_t> {
        if (code_bits > 0 && code_bits <= 23) {
            uint64_t h = serialize_header_bits(vld, jump, embed, loops, cond, dly,
                static_cast<uint8_t>(inc & 0xF), embed_end_bits, inc_embed_bits);
            uint64_t c = 0;
            for (uint8_t b : code_bytes) c = (c << 8) | b;
            if (code_bits < 64) c &= ((1ULL << code_bits) - 1);
            uint64_t total = (h << code_bits) | c;
            int total_width = 41 + code_bits;
            int num_bytes = (total_width + 7) / 8;
            std::vector<uint8_t> ret;
            for (int i = num_bytes - 1; i >= 0; --i)
                ret.push_back(static_cast<uint8_t>((total >> (i * 8)) & 0xFF));
            return ret;
        } else {
            auto header = serialize_header(vld, jump, embed, loops, cond, dly,
                static_cast<uint8_t>(inc & 0xF), embed_end_bits, inc_embed_bits);
            header.insert(header.end(), code_bytes.begin(), code_bytes.end());
            return header;
        }
    };

    auto serialize_ur_wdata_inst = [&](uint8_t vld, uint8_t jump, uint8_t embed,
        uint16_t loops, uint8_t cond, uint8_t dly, uint8_t inc,
        uint8_t embed_end_bits, uint8_t inc_embed_bits, uint32_t code_28) -> std::vector<uint8_t> {
        uint64_t header = serialize_header_bits(vld, jump, embed, loops, cond, dly,
                                                inc, embed_end_bits, inc_embed_bits);
        unsigned __int128 inst_69 = (static_cast<unsigned __int128>(header) << 28) |
            static_cast<unsigned __int128>(code_28 & 0x0FFFFFFFu);
        std::vector<uint8_t> bytes;
        for (int i = 8; i >= 0; --i)
            bytes.push_back(static_cast<uint8_t>((inst_69 >> (i * 8)) & 0xFF));
        return bytes;
    };

    const uint16_t loops_inner = (para_total_steps >= 1) ? static_cast<uint16_t>(para_total_steps - 1) : 0;
    const uint8_t  inc_inner   = 1u;
    constexpr uint16_t kLogicalPipelineExtra = 4;
    const bool use_bc_unroll = (para_bank_mode == ToCopyBankMode::SAME_BANK);
    const uint8_t num_slots  = use_bc_unroll ? 2u : 1u;

    uint32_t nop_bank_global = 7u;
    if (use_bc_unroll) {
        const uint32_t in_bank  = para_input_bank  & 0x7u;
        const uint32_t out_bank = para_output_bank & 0x7u;
        for (uint32_t b = 7; ; b = (b == 0) ? 7 : b - 1) {
            if (b != in_bank && b != out_bank) { nop_bank_global = b; break; }
            if (b == 0) { nop_bank_global = 7; break; }
        }
    }
    const uint32_t nop_read_code = (nop_bank_global << 1) | 0u;

    // 1) UR Read Port 0: input (IQ 14)
    {
        IqInstructionFile file;
        file.para_iq_id      = iq::IQ_ID_UR_RDATA_0;
        file.para_iq_name    = iq::IQ_NAME_UR_RDATA_0;
        file.para_width_bits = 41 + 12;

        const uint32_t base_code = extract_ur_read_port0_code(para_ur_read_crus[0]);
        auto zero_code = serialize_code(0, 12);

        if (use_bc_unroll) {
            auto nop_bytes  = serialize_code(nop_read_code, 12);
            auto read_bytes = serialize_code(base_code, 12);

            auto init = make_instruction(1, 0, 1u, loops_inner, 0, 0, 1, 0u, 0u, zero_code, 12);
            file.para_data.insert(file.para_data.end(), init.begin(), init.end());

            auto b1 = make_instruction(1, 0, 0u, 0, 0, 0, 0, 0u, 0x01, read_bytes, 12);
            file.para_data.insert(file.para_data.end(), b1.begin(), b1.end());

            auto b2 = make_instruction(1, 0, 0u, 0, 0, 0, 0, 0x01, 0x01, nop_bytes, 12);
            file.para_data.insert(file.para_data.end(), b2.begin(), b2.end());
        } else {
            auto code_bytes = serialize_code(base_code, 12);
            auto inst = make_instruction(1, 0, 0u, loops_inner, 0, 0, inc_inner, 0u, 0u, code_bytes, 12);
            file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
        }

        auto end_inst = make_instruction(1, 1, 0, 0, 0, 0, 0, 0u, 0u, zero_code, 12);
        file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
        files.push_back(file);
    }

    // 2) RtoVR 路由：读路由 + 写路由
    for (const auto& route : para_rtovr_routes) {
        const uint8_t target_port = route.first;
        const uint8_t src_id      = route.second;
        const bool is_read_route = (target_port != 35);

        IqInstructionFile file;
        file.para_iq_id      = is_read_route ? iq::IQ_ID_RTOVR_TO_FPMUL : iq::IQ_ID_RTOVR_TO_UR;
        file.para_iq_name    = "rtovr-" + std::to_string(target_port);
        file.para_width_bits = 41 + 7;

        const uint8_t route_code = static_cast<uint8_t>((src_id << 1) | 1u);
        auto code_bytes = serialize_code(route_code, 7);
        auto zero_code  = serialize_code(0u, 7);

        if (use_bc_unroll && is_read_route) {
            uint16_t total_data_cycles = static_cast<uint16_t>(static_cast<uint32_t>(num_slots) * (loops_inner + 1));
            uint16_t route_loops = static_cast<uint16_t>(total_data_cycles - 1);
            uint32_t total_dly = 2u;
            emit_dummy_for_long_delay(total_dly, [&]() {
                auto dummy = make_instruction(1, 0, 0, 0, 0, kMaxDlyField, 0, 0, 0, zero_code, 7);
                file.para_data.insert(file.para_data.end(), dummy.begin(), dummy.end());
            });
            uint8_t dly = static_cast<uint8_t>(total_dly);
            auto inst = make_instruction(1, 0, 0u, route_loops, 0, dly, 0, 0u, 0u, code_bytes, 7);
            file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
        } else {
            uint16_t route_loops;
            if (use_bc_unroll) {
                uint16_t tdc = static_cast<uint16_t>(static_cast<uint32_t>(num_slots) * (loops_inner + 1));
                route_loops = static_cast<uint16_t>(tdc + kLogicalPipelineExtra - 1);
            } else {
                route_loops = is_read_route ? loops_inner : static_cast<uint16_t>(loops_inner + kLogicalPipelineExtra);
            }
            uint8_t base_dly = is_read_route ? 1u : (use_bc_unroll ? 5u : 3u);
            uint32_t total_dly = static_cast<uint32_t>(base_dly);
            emit_dummy_for_long_delay(total_dly, [&]() {
                auto dummy = make_instruction(1, 0, 0, 0, 0, kMaxDlyField, 0, 0, 0, zero_code, 7);
                file.para_data.insert(file.para_data.end(), dummy.begin(), dummy.end());
            });
            uint8_t dly = static_cast<uint8_t>(total_dly);
            auto inst = make_instruction(1, 0, 0u, route_loops, 0, dly, 0, 0u, 0u, code_bytes, 7);
            file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
        }

        auto end_code = serialize_code(0u, 7);
        auto end_inst = make_instruction(1, 1, 0, 0, 0, 0, 0, 0u, 0u, end_code, 7);
        file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
        files.push_back(file);
    }

    // 3) 计算单元指令 IQ
    {
        IqInstructionFile file;
        file.para_iq_id      = para_compute_iq_id;
        file.para_iq_name    = para_compute_iq_name;
        file.para_width_bits = 41 + para_code_bits;

        auto code_bytes = serialize_code(para_compute_inst, para_code_bits);
        auto zero_code  = serialize_code(0u, para_code_bits);

        uint32_t total_dly = use_bc_unroll ? 3u : 2u;
        emit_dummy_for_long_delay(total_dly, [&]() {
            auto dummy = make_instruction(1, 0, 0, 0, 0, kMaxDlyField, 0, 0, 0, zero_code, para_code_bits);
            file.para_data.insert(file.para_data.end(), dummy.begin(), dummy.end());
        });
        uint8_t dly = static_cast<uint8_t>(total_dly);

        uint16_t logic_loops;
        if (use_bc_unroll) {
            uint16_t tdc = static_cast<uint16_t>(static_cast<uint32_t>(num_slots) * (loops_inner + 1));
            logic_loops = static_cast<uint16_t>(tdc + kLogicalPipelineExtra - 1);
        } else {
            logic_loops = static_cast<uint16_t>(loops_inner + kLogicalPipelineExtra);
        }
        auto inst = make_instruction(1, 0, 0u, logic_loops, 0, dly, 0, 0u, 0u, code_bytes, para_code_bits);
        file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());

        auto end_code2 = serialize_code(0u, para_code_bits);
        auto end_inst  = make_instruction(1, 1, 0, 0, 0, 0, 0, 0u, 0u, end_code2, para_code_bits);
        file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
        files.push_back(file);
    }

    // 4) UR Write Port 0: output (IQ 18)
    {
        IqInstructionFile file;
        file.para_iq_id      = iq::IQ_ID_UR_WDATA_0;
        file.para_iq_name    = iq::IQ_NAME_UR_WDATA_0;
        file.para_width_bits = 41 + 28;

        uint32_t base_code = extract_ur_write_port0_code(para_ur_write_crus[0]);

        if (use_bc_unroll) {
            auto dummy = serialize_ur_wdata_inst(1, 0, 7u, 0, 0, 0, 0, 0, 0, 0);
            file.para_data.insert(file.para_data.end(), dummy.begin(), dummy.end());

            auto init = serialize_ur_wdata_inst(1, 0, 1u, loops_inner, 0, 0, 1, 0u, 0u, 0);
            file.para_data.insert(file.para_data.end(), init.begin(), init.end());

            auto b1 = serialize_ur_wdata_inst(1, 0, 0u, 0, 0, 3, 0, 0u, 0x01, 0);
            file.para_data.insert(file.para_data.end(), b1.begin(), b1.end());

            auto b2 = serialize_ur_wdata_inst(1, 0, 0u, 0, 0, 0, 0, 0x01, 0x01, base_code);
            file.para_data.insert(file.para_data.end(), b2.begin(), b2.end());
        } else {
            auto inst = serialize_ur_wdata_inst(1, 0, 0u, loops_inner, 0, 4, inc_inner, 0u, 0u, base_code);
            file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
        }

        auto end_inst = serialize_ur_wdata_inst(1, 1, 0, 0, 0, 2, 0, 0u, 0u, 0);
        file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
        files.push_back(file);
    }

    return files;
}

// ==========================================
// serialize
// ==========================================
std::vector<uint8_t> ToCopyInstructions::serialize() const {
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
    result.push_back(para_compute_inst);
    result.push_back(static_cast<uint8_t>(para_in_dtype));
    result.push_back(static_cast<uint8_t>(para_out_dtype));
    for (const auto& r : para_rtovr_routes) {
        result.push_back(r.first);
        result.push_back(r.second);
    }
    for (const auto& cru : para_ur_read_crus) {
        uint32_t ur_words[6];
        iq::UrIq::pack_cru_ur_words(cru, ur_words);
        for (int i = 0; i < 6; ++i) push_u32(ur_words[i]);
    }
    for (const auto& cru : para_ur_write_crus) {
        uint32_t ur_words[6];
        iq::UrIq::pack_cru_ur_words(cru, ur_words);
        for (int i = 0; i < 6; ++i) push_u32(ur_words[i]);
    }
    return result;
}

// ==========================================
// calculate_golden
// ==========================================
bool ToCopyOp::calculate_golden(const app::AppOperatorRequest& req, std::vector<uint8_t>& golden_bin) {
    golden_bin.clear();
    auto it = req.para_inputs.find("input_tensor");
    if (it == req.para_inputs.end()) return false;
    const auto& t = it->second;

    app::AppDataType out_dtype = t.para_dtype;
    auto oit = req.para_parameters.find("out_dtype");
    if (oit != req.para_parameters.end() && !oit->second.para_data_i32.empty()) {
        out_dtype = static_cast<app::AppDataType>(oit->second.para_data_i32[0]);
    }

    size_t count = 0;
    if (t.para_dtype == app::AppDataType::FLOAT32 || t.para_dtype == app::AppDataType::FLOAT16) {
        count = t.para_data_f32.size();
    } else {
        count = t.para_data_i32.size();
    }

    for (size_t i = 0; i < count; ++i) {
        // 1. 获取输入值并转换为浮点数或整数
        float fval = 0.0f;
        int32_t ival = 0;

        if (t.para_dtype == app::AppDataType::FLOAT32) {
            fval = t.para_data_f32[i];
            ival = static_cast<int32_t>(fval); // 硬件转换默认为向零截断 (truncation)
        } else if (t.para_dtype == app::AppDataType::FLOAT16) {
            // 先量化到 FP16 精度再转回 FP32，与硬件行为一致
            uint16_t hbits = app::float32_to_float16(t.para_data_f32[i]);
            fval = app::float16_to_float32(hbits);
            ival = static_cast<int32_t>(fval); // 硬件转换默认为向零截断
        } else {
            ival = t.para_data_i32[i];
            fval = static_cast<float>(ival);
        }

        // 2. 根据目标 Dtype 进行写入
        if (out_dtype == app::AppDataType::FLOAT32) {
            uint32_t bits;
            std::memcpy(&bits, &fval, sizeof(float));
            golden_bin.push_back(bits & 0xFF);
            golden_bin.push_back((bits >> 8) & 0xFF);
            golden_bin.push_back((bits >> 16) & 0xFF);
            golden_bin.push_back((bits >> 24) & 0xFF);
        } else if (out_dtype == app::AppDataType::FLOAT16) {
            uint16_t hbits = app::float32_to_float16(fval);
            if (t.para_dtype != out_dtype) {
                // 格式转换模式：非 LOGIC COPY。各个 FP16 数据元素在 128bit 中以 32bit 为步长（Pitch）对齐
                golden_bin.push_back(hbits & 0xFF);
                golden_bin.push_back((hbits >> 8) & 0xFF);
                golden_bin.push_back(0);
                golden_bin.push_back(0);
            } else {
                // LOGIC COPY：数据元素连续存储 (16-bit pitch)
                golden_bin.push_back(hbits & 0xFF);
                golden_bin.push_back((hbits >> 8) & 0xFF);
            }
        } else {
            // INT32
            uint32_t bits;
            std::memcpy(&bits, &ival, sizeof(int32_t));
            golden_bin.push_back(bits & 0xFF);
            golden_bin.push_back((bits >> 8) & 0xFF);
            golden_bin.push_back((bits >> 16) & 0xFF);
            golden_bin.push_back((bits >> 24) & 0xFF);
        }
    }

    return true;
}

// ==========================================
// generate_data
// ==========================================
bool ToCopyOp::generate_data(const app::AppOperatorRequest& req,
                             std::vector<uint8_t>& input_bin,
                             std::vector<uint8_t>& other_bin,
                             std::vector<uint8_t>& alpha_bin) {
    input_bin.clear();
    other_bin.clear();
    alpha_bin.clear();

    auto it = req.para_inputs.find("input_tensor");
    if (it == req.para_inputs.end()) return false;
    const auto& t = it->second;

    app::AppDataType out_dtype = t.para_dtype;
    auto oit = req.para_parameters.find("out_dtype");
    if (oit != req.para_parameters.end() && !oit->second.para_data_i32.empty()) {
        out_dtype = static_cast<app::AppDataType>(oit->second.para_data_i32[0]);
    }

    if (t.para_dtype == app::AppDataType::FLOAT32) {
        for (float v : t.para_data_f32) {
            uint32_t bits;
            std::memcpy(&bits, &v, sizeof(float));
            input_bin.push_back(bits & 0xFF);
            input_bin.push_back((bits >> 8) & 0xFF);
            input_bin.push_back((bits >> 16) & 0xFF);
            input_bin.push_back((bits >> 24) & 0xFF);
        }
    } else if (t.para_dtype == app::AppDataType::FLOAT16) {
        for (float v : t.para_data_f32) {
            uint16_t hbits = app::float32_to_float16(v);
            if (t.para_dtype != out_dtype) {
                // 格式转换模式：非 LOGIC COPY。各个 FP16 数据元素在 128bit 中以 32bit 为步长（Pitch）对齐
                input_bin.push_back(hbits & 0xFF);
                input_bin.push_back((hbits >> 8) & 0xFF);
                input_bin.push_back(0);
                input_bin.push_back(0);
            } else {
                // LOGIC COPY：数据元素连续存储 (16-bit pitch)
                input_bin.push_back(hbits & 0xFF);
                input_bin.push_back((hbits >> 8) & 0xFF);
            }
        }
    } else {
        for (int32_t v : t.para_data_i32) {
            uint32_t bits;
            std::memcpy(&bits, &v, sizeof(int32_t));
            input_bin.push_back(bits & 0xFF);
            input_bin.push_back((bits >> 8) & 0xFF);
            input_bin.push_back((bits >> 16) & 0xFF);
            input_bin.push_back((bits >> 24) & 0xFF);
        }
    }
    return true;
}

// ==========================================
// fill_random_request
// ==========================================
void ToCopyOp::fill_random_request(app::AppOperatorRequest& req,
                                   std::mt19937& rng,
                                   std::uniform_real_distribution<float>& data_dist,
                                   const std::string& dtype_str) {
    req.para_operator = "_to_copy";

    const uint32_t total = req.para_dimensions.para_n * req.para_dimensions.para_c *
                           req.para_dimensions.para_h * req.para_dimensions.para_w;

    // 解析输入与输出 Dtype。我们支持通过随机种子与 dtype_str 随机生成不同的转换模式
    app::AppDataType in_dtype = app::AppDataType::FLOAT32;
    app::AppDataType out_dtype = app::AppDataType::FLOAT32;

    if (dtype_str == "fp32" || dtype_str == "float32") {
        in_dtype = app::AppDataType::FLOAT32;
    } else if (dtype_str == "fp16" || dtype_str == "float16") {
        in_dtype = app::AppDataType::FLOAT16;
    } else if (dtype_str == "int32") {
        in_dtype = app::AppDataType::INT32;
    }

    // 随机挑选输出的 DataType 覆盖 Cast 分支
    std::vector<app::AppDataType> dtypes = {app::AppDataType::FLOAT32, app::AppDataType::FLOAT16, app::AppDataType::INT32};
    std::uniform_int_distribution<size_t> select_dist(0, dtypes.size() - 1);
    out_dtype = dtypes[select_dist(rng)];

    app::AppTensorDesc input_desc;
    input_desc.para_name  = "input_tensor";
    input_desc.para_dtype = in_dtype;
    input_desc.para_shape = {req.para_dimensions.para_n, req.para_dimensions.para_c,
                             req.para_dimensions.para_h, req.para_dimensions.para_w};

    if (in_dtype == app::AppDataType::FLOAT32 || in_dtype == app::AppDataType::FLOAT16) {
        for (uint32_t i = 0; i < total; ++i) {
            input_desc.para_data_f32.push_back(data_dist(rng));
        }
    } else {
        std::uniform_int_distribution<int32_t> int_dist(-1000, 1000);
        for (uint32_t i = 0; i < total; ++i) {
            input_desc.para_data_i32.push_back(int_dist(rng));
        }
    }
    req.para_inputs["input_tensor"] = input_desc;

    // 分配不同 bank
    std::vector<int> banks = {0, 1, 2, 3, 4, 5, 6, 7};
    std::shuffle(banks.begin(), banks.end(), rng);
    int input_bank  = banks[0];
    int output_bank = banks[1];

    auto set_int_param = [&req](const std::string& name, int val) {
        app::AppTensorDesc d;
        d.para_data_i32.push_back(val);
        req.para_parameters[name] = d;
    };
    set_int_param("input_bank",  input_bank);
    set_int_param("output_bank", output_bank);
    set_int_param("input_addr",  0);
    set_int_param("output_addr", 0);
    set_int_param("out_dtype",   static_cast<int>(out_dtype));
}

REGISTER_OP(ToCopyOp);

} // namespace op
} // namespace aiacc
