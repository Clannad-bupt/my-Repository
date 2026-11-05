#include "tvgen.h"
#include "../tools/hex_converter.h"

/**
 * @file tvgen_core.cpp
 * @brief TVGen 框架核心部分：主入口 + 流程控制 + 文件写入
 */

#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <map>
#include <set>
#include <sys/stat.h>
#include <random>
#include <sstream>
#include <map>

#include "fp16_utils.h"
#include "../op/common/op_registry.h"
#include "simulator_config_gen.h"

using namespace aiacc;
using namespace aiacc::app;
using namespace aiacc::op;

// ==========================================
// 构造函数
// ==========================================

Tvgen::Tvgen(const TvgenConfig& para_config) : para_config_(para_config) {}

// ==========================================
// 主入口
// ==========================================

TvgenResult Tvgen::generate(const AppOperatorRequest& para_req) {
    TvgenResult result;
    
    // 创建输出目录
    (void)std::system((std::string("mkdir -p ") + para_config_.para_output_dir).c_str());
    
    // 1. 获取算子
    OpBase* op = OpRegistry::get_instance().get_op(para_req.para_operator);
    if (!op) {
        result.para_error_msg = "Failed to get operator: " + para_req.para_operator;
        return result;
    }
    
    // 2. 编译
    std::vector<uint8_t> inst_bin;
    std::vector<op::IqInstructionFile> iq_files;
    if (!compile(para_req, inst_bin, iq_files)) {
        result.para_error_msg = "Compiler failed";
        return result;
    }
    
    // 3. 生成数据
    std::vector<uint8_t> input_bin, other_bin, alpha_bin;
    if (!op->generate_data(para_req, input_bin, other_bin, alpha_bin)) {
        result.para_error_msg = "DataGen failed";
        return result;
    }
    
    // 4. 计算预期结果
    std::vector<uint8_t> golden_bin;
    if (!op->calculate_golden(para_req, golden_bin)) {
        result.para_error_msg = "GoldenCalc failed";
        return result;
    }
    
    // 5. 写入数据文件
    result.para_input_bin_path = para_config_.para_output_dir + "/input.bin";
    result.para_other_bin_path = para_config_.para_output_dir + "/other.bin";
    result.para_alpha_bin_path = para_config_.para_output_dir + "/alpha.bin";
    result.para_golden_bin_path = para_config_.para_output_dir + "/golden.bin";
    
    if (!write_bin_file(result.para_input_bin_path, input_bin) ||
        !write_bin_file(result.para_other_bin_path, other_bin) ||
        !write_bin_file(result.para_alpha_bin_path, alpha_bin) ||
        !write_bin_file(result.para_golden_bin_path, golden_bin)) {
        result.para_error_msg = "Failed to write data files";
        return result;
    }
    
    // 6. 写入指令文件（按 IQ 分层，输出到 instructions/ 子目录）
    const std::string inst_dir = para_config_.para_output_dir + "/instructions";
    const std::string hex_inst_dir = para_config_.para_output_dir + "/hex_instructions"; // New Hex Dir
    
    (void)std::system((std::string("mkdir -p ") + inst_dir).c_str());
    (void)std::system((std::string("mkdir -p ") + hex_inst_dir).c_str());

    std::map<std::string, std::string> iq_init;  // SMC Simulator iq_init
    result.para_iq_inst_paths.clear();
    for (const auto& f : iq_files) {
        const std::string path = inst_dir + "/" + f.get_filename();
        if (!write_bin_file(path, f.para_data)) {
            result.para_error_msg = "Failed to write IQ instruction file: " + path;
            return result;
        }
        
        // Write Hex File for Simulator
        std::string hex_path = hex_inst_dir + "/" + f.get_filename_no_ext() + ".hex";
        size_t width = (f.para_width_bits + 7) / 8; // Calculate byte width
        if (!tools::HexConverter::write_instruction_hex(hex_path, f.para_data, width, f.para_width_bits)) {
             std::cout << "[Warning] Failed to write Hex file: " << hex_path << "\n";
        }

        std::string sim_name = get_simulator_iq_name(f.para_iq_id, f.para_iq_name);
        if (!sim_name.empty()) {
            iq_init[sim_name] = "hex_instructions/" + f.get_filename_no_ext() + ".hex";
        }

        // 新架构：add_op.cpp 已经按端口生成独立文件（如 iq_24_rtovr-3.hex）
        // 直接使用文件名中的端口号作为 IQ 名称
        std::string fname_no_ext = f.get_filename_no_ext();
        if (fname_no_ext.find("rtovr-") != std::string::npos) {
            // 提取端口号：iq_24_rtovr-3 -> rtovr-3
            size_t pos = fname_no_ext.find("rtovr-");
            if (pos != std::string::npos) {
                std::string port_name = fname_no_ext.substr(pos);  // "rtovr-3"
                iq_init[port_name] = "hex_instructions/" + fname_no_ext + ".hex";
            }
        }
        
        result.para_iq_inst_paths.push_back(path);
    }
    
    // Write Memory Hex Files for Simulator UR Init
    // For bank_conflict mode: all data in Bank 0 at different addresses (0, 64, 128)
    // For default mode: separate banks (0, 1, 2)
    
    // Compute max_addr from golden (output) data size
    const size_t bytes_per_ur_addr = 16;  // 128-bit UR word
    int output_max_addr = static_cast<int>((golden_bin.size() + bytes_per_ur_addr - 1) / bytes_per_ur_addr);
    if (output_max_addr < 32) output_max_addr = 32;  // minimum
    
    if (para_req.para_layout_mode == "bank_conflict") {
        // Per-tensor bank assignment: each tensor independently assigned to a bank
        int ib = (para_req.para_bc_input_bank  >= 0) ? para_req.para_bc_input_bank  : 0;
        int ob = (para_req.para_bc_other_bank  >= 0) ? para_req.para_bc_other_bank  : 0;
        int ab = (para_req.para_bc_alpha_bank  >= 0) ? para_req.para_bc_alpha_bank  : 0;
        int xb = (para_req.para_bc_output_bank >= 0) ? para_req.para_bc_output_bank : 1;
        
        const size_t bytes_per_addr = 16;  // 128-bit UR word
        size_t input_words = (input_bin.size()  + bytes_per_addr - 1) / bytes_per_addr;
        size_t other_words = (other_bin.size()  + bytes_per_addr - 1) / bytes_per_addr;
        
        // Alpha broadcast padding to fill 128-bit UR word
        std::vector<uint8_t> alpha_padded = alpha_bin;
        if (!alpha_padded.empty() && alpha_padded.size() < bytes_per_addr) {
            size_t orig_size = alpha_padded.size();
            alpha_padded.resize(bytes_per_addr);
            for (size_t i = orig_size; i < bytes_per_addr; ++i) {
                alpha_padded[i] = alpha_bin[i % orig_size];
            }
        }
        
        // Compute alpha word count (1 for broadcast scalars like add, full-size for div's x0)
        size_t alpha_words = (!alpha_bin.empty()) ?
            ((alpha_bin.size() + bytes_per_addr - 1) / bytes_per_addr) : 0;
        if (alpha_words < 1 && !alpha_bin.empty()) alpha_words = 1;

        // Compute per-bank addresses (same sequential packing as add_op.cpp)
        std::map<int, size_t> bank_next;  // bank -> next available byte offset
        size_t input_byte_off = bank_next[ib]; bank_next[ib] += input_words * bytes_per_addr;
        size_t other_byte_off = bank_next[ob]; bank_next[ob] += other_words * bytes_per_addr;
        size_t alpha_byte_off = 0;
        if (!alpha_bin.empty()) {
            alpha_byte_off = bank_next[ab]; 
            bank_next[ab] += alpha_words * bytes_per_addr;
        }
        // output has no initial data, but reserve space tracking
        // (not needed for data writing, but note it for completeness)
        
        // Handle const_tensor (e.g., div's constant 2.0)
        int cb_bc = -1;
        size_t const_byte_off = 0;
        std::vector<uint8_t> const_bc_data;
        {
            auto cit = para_req.para_parameters.find("const2_bank");
            if (cit != para_req.para_parameters.end() && !cit->second.para_data_i32.empty()) {
                cb_bc = cit->second.para_data_i32[0];
            }
        }
        if (cb_bc >= 0) {
            auto cit = para_req.para_parameters.find("const_tensor");
            if (cit != para_req.para_parameters.end()) {
                for (float v : cit->second.para_data_f32) {
                    if (cit->second.para_dtype == app::AppDataType::FLOAT16) {
                        uint16_t bits = app::float32_to_float16(v);
                        const_bc_data.push_back(bits & 0xFF);
                        const_bc_data.push_back((bits >> 8) & 0xFF);
                    } else {
                        uint32_t bits; std::memcpy(&bits, &v, sizeof(float));
                        const_bc_data.push_back(bits & 0xFF);
                        const_bc_data.push_back((bits >> 8) & 0xFF);
                        const_bc_data.push_back((bits >> 16) & 0xFF);
                        const_bc_data.push_back((bits >> 24) & 0xFF);
                    }
                }
                // Get const2_addr from parameters
                int ca_bc = 0;
                auto cait = para_req.para_parameters.find("const2_addr");
                if (cait != para_req.para_parameters.end() && !cait->second.para_data_i32.empty()) {
                    ca_bc = cait->second.para_data_i32[0];
                } else {
                    ca_bc = static_cast<int>(bank_next[cb_bc] / bytes_per_addr);
                    bank_next[cb_bc] += 1 * bytes_per_addr;
                }
                const_byte_off = static_cast<size_t>(ca_bc) * bytes_per_addr;
            }
        }

        // Build per-bank data buffers
        // Collect all tensors going to each bank in address order
        struct TensorInfo { size_t byte_off; const std::vector<uint8_t>* data; size_t padded_bytes; };
        std::map<int, std::vector<TensorInfo>> bank_tensors;
        bank_tensors[ib].push_back({input_byte_off, &input_bin,    input_words * bytes_per_addr});
        bank_tensors[ob].push_back({other_byte_off, &other_bin,    other_words * bytes_per_addr});
        if (!alpha_bin.empty()) {
            bank_tensors[ab].push_back({alpha_byte_off, &alpha_padded, alpha_words * bytes_per_addr});
        }
        if (cb_bc >= 0 && !const_bc_data.empty()) {
            bank_tensors[cb_bc].push_back({const_byte_off, &const_bc_data, 1 * bytes_per_addr});
        }
        
        // For each bank, create a contiguous data buffer covering all tensors
        std::set<int> data_bank_ids;
        for (auto& [bank_id, tensors] : bank_tensors) {
            // Find total size needed for this bank
            size_t total_bytes = 0;
            for (auto& t : tensors) {
                size_t end = t.byte_off + t.padded_bytes;
                if (end > total_bytes) total_bytes = end;
            }
            std::vector<uint8_t> bank_data(total_bytes, 0);
            for (auto& t : tensors) {
                size_t copy_len = std::min(t.data->size(), t.padded_bytes);
                std::memcpy(bank_data.data() + t.byte_off, t.data->data(), copy_len);
            }
            
            std::ostringstream ur_fname;
            ur_fname << "/ur_" << std::setfill('0') << std::setw(2) << bank_id << ".hex";
            tools::HexConverter::write_memory_hex(
                para_config_.para_output_dir + ur_fname.str(), bank_data);
            data_bank_ids.insert(bank_id);
        }
        
        // Build bank_names for simulator config
        std::map<int, std::string> bank_names;
        auto append_name = [&](int bank, const std::string& name) {
            if (bank_names.find(bank) != bank_names.end())
                bank_names[bank] += "/" + name;
            else
                bank_names[bank] = name;
        };
        append_name(ib, "input");
        append_name(ob, "other");
        if (!alpha_bin.empty()) append_name(ab, "alpha");
        append_name(xb, "output");
        if (cb_bc >= 0) append_name(cb_bc, "const2");
        
        // 提取 dtype 并转换为字符串
        std::string dtype_str = "fp32";  // 默认
        if (!para_req.para_inputs.empty()) {
            auto it = para_req.para_inputs.find("input_tensor");
            if (it == para_req.para_inputs.end()) it = para_req.para_inputs.find("input");
            if (it != para_req.para_inputs.end()) {
                switch (it->second.para_dtype) {
                    case app::AppDataType::FLOAT16: dtype_str = "fp16"; break;
                    case app::AppDataType::FLOAT32: dtype_str = "fp32"; break;
                    case app::AppDataType::INT32: dtype_str = "int32"; break;
                    case app::AppDataType::INT4: dtype_str = "int4"; break;
                    default: dtype_str = "fp32"; break;
                }
            }
        }
        
        // Compute output_start_addr: how many addresses in the output bank
        // are already occupied by other tensors (e.g. alpha)
        // For Div: use the operator-provided output_addr (accounts for all 5 operands)
        int output_start_addr = static_cast<int>(bank_next[xb] / bytes_per_addr);
        if (para_req.para_operator == "Div") {
            auto oa_it = para_req.para_parameters.find("output_addr");
            if (oa_it != para_req.para_parameters.end() && !oa_it->second.para_data_i32.empty()) {
                output_start_addr = oa_it->second.para_data_i32[0];
            }
        }
        
        // Bank conflict EMBED mode has num_slots-cycle period (typically 2).
        // max_cycles must be large enough for the slowest IQ (write port) to finish.
        // Estimate: total_steps * num_slots + max_pipeline_delay + drain_margin
        int bc_max_cycles = output_max_addr * 4 + 100;  // generous: 4x steps + margin
        if (para_req.para_operator == "Div") {
            // 动态计算: 每 step = iters*15+7 cycles (+ bank conflict 额外 cycle)
            int div_iters = 3;  // default
            auto nit = para_req.para_parameters.find("newton_iters");
            if (nit != para_req.para_parameters.end() && !nit->second.para_data_i32.empty())
                div_iters = nit->second.para_data_i32[0];
            auto nit2 = para_req.para_parameters.find("iters");
            if (nit2 != para_req.para_parameters.end() && !nit2->second.para_data_i32.empty())
                div_iters = nit2->second.para_data_i32[0];
            int cycles_per_step = div_iters * 16 + 8 + 10;  // 16=15+1(conflict), 8=7+1, +10 margin
            bc_max_cycles = output_max_addr * cycles_per_step + 200;
        }
        
        std::vector<int> data_banks_vec(data_bank_ids.begin(), data_bank_ids.end());
        write_simulator_config(para_config_.para_output_dir, iq_init, 
                               data_banks_vec, xb, bc_max_cycles, dtype_str, 
                               output_max_addr, bank_names, output_start_addr, para_req.para_operator);
    } else {
        int ib = 0, ob = 1, ab = 2, xb = 3, cb = -1;
        
        auto get_bank = [&](const std::string& key, int def) -> int {
            auto it = para_req.para_parameters.find(key);
            if (it != para_req.para_parameters.end() && !it->second.para_data_i32.empty()) {
                return it->second.para_data_i32[0];
            }
            return def;
        };
        ib = get_bank("input_bank", 0);
        ob = get_bank("other_bank", 1);
        ab = get_bank("alpha_bank", 2);
        cb = get_bank("const2_bank", -1);
        xb = get_bank("output_bank", 3);

        // 读取地址偏移（默认 0，算子可通过 para_parameters 指定随机地址）
        int ia = get_bank("input_addr", 0);
        int oa = get_bank("other_addr", 0);
        int aa = get_bank("alpha_addr", 0);
        int xa = get_bank("output_addr", 0);
        int ca = get_bank("const2_addr", 0);

        auto fmt_hex = [](int b) -> std::string {
            std::ostringstream os;
            os << "/ur_" << std::setfill('0') << std::setw(2) << b << ".hex";
            return os.str();
        };

        // 辅助：在数据前面填充 addr * 16 字节的零，使数据被放置在正确的 UR 地址偏移处
        auto pad_data_to_addr = [&](const std::vector<uint8_t>& data, int addr) -> std::vector<uint8_t> {
            if (addr <= 0) return data;
            size_t pad_bytes = static_cast<size_t>(addr) * bytes_per_ur_addr;
            std::vector<uint8_t> padded(pad_bytes, 0);
            padded.insert(padded.end(), data.begin(), data.end());
            return padded;
        };

        // =========================================================================
        // 按 bank 聚合数据：同一 bank 可能有多个 tensor（如 mul 的 input 和 output
        // 可能随机分到同一 bank），需要像 bank_conflict 模式一样根据地址偏移来
        // 拼装整个 bank 的内存映像。
        // =========================================================================

        // Alpha broadcast padding
        std::vector<uint8_t> alpha_padded = alpha_bin;
        if (!alpha_bin.empty() && alpha_padded.size() < 16) {
            size_t orig_size = alpha_padded.size();
            alpha_padded.resize(16);
            for (size_t i = orig_size; i < 16; ++i) {
                alpha_padded[i] = alpha_bin[i % orig_size];
            }
        }

        // 收集每个 bank 上的所有 tensor 片段
        struct TensorSlice { int addr; const std::vector<uint8_t>* data; };
        std::map<int, std::vector<TensorSlice>> bank_slices;

        bank_slices[ib].push_back({ia, &input_bin});
        bank_slices[ob].push_back({oa, &other_bin});
        if (!alpha_bin.empty()) {
            bank_slices[ab].push_back({aa, &alpha_padded});
        }
        // output 没有初始数据，不需要写入

        // 为每个 bank 生成连续的内存映像
        for (auto& [bank_id, slices] : bank_slices) {
            // 计算这个 bank 的总大小
            size_t total_bytes = 0;
            for (auto& s : slices) {
                size_t end = static_cast<size_t>(s.addr) * bytes_per_ur_addr + s.data->size();
                if (end > total_bytes) total_bytes = end;
            }
            std::vector<uint8_t> bank_data(total_bytes, 0);
            for (auto& s : slices) {
                size_t offset = static_cast<size_t>(s.addr) * bytes_per_ur_addr;
                std::memcpy(bank_data.data() + offset, s.data->data(),
                            std::min(s.data->size(), total_bytes - offset));
            }
            tools::HexConverter::write_memory_hex(
                para_config_.para_output_dir + fmt_hex(bank_id), bank_data);
        }
        
        // 提取 dtype 并转换为字符串
        std::string dtype_str = "fp32";  // 默认
        if (!para_req.para_inputs.empty()) {
            auto it = para_req.para_inputs.find("input_tensor");
            if (it == para_req.para_inputs.end()) it = para_req.para_inputs.find("input");
            if (it != para_req.para_inputs.end()) {
                switch (it->second.para_dtype) {
                    case app::AppDataType::FLOAT16: dtype_str = "fp16"; break;
                    case app::AppDataType::FLOAT32: dtype_str = "fp32"; break;
                    case app::AppDataType::INT32: dtype_str = "int32"; break;
                    case app::AppDataType::INT4: dtype_str = "int4"; break;
                    default: dtype_str = "fp32"; break;
                }
            }
        }

        // Collect unique data bank ids
        std::set<int> data_bank_set;
        for (auto& [bank_id, _] : bank_slices) data_bank_set.insert(bank_id);
        std::map<int, std::string> bank_names;
        bank_names[ib] = "input";
        if (bank_names.count(ob)) bank_names[ob] += "/other"; else bank_names[ob] = "other";
        if (!alpha_bin.empty()) {
            if (bank_names.count(ab)) bank_names[ab] += "/alpha"; else bank_names[ab] = "alpha";
        }
        if (bank_names.count(xb)) bank_names[xb] += "/output"; else bank_names[xb] = "output";

        // Write const_tensor if const2_bank is specified
        if (cb != -1) {
            auto it = para_req.para_parameters.find("const_tensor");
            if (it != para_req.para_parameters.end()) {
                std::vector<uint8_t> const_bin;
                for (float v : it->second.para_data_f32) {
                    if (it->second.para_dtype == app::AppDataType::FLOAT16) {
                        uint16_t bits = app::float32_to_float16(v);
                        const_bin.push_back(bits & 0xFF);
                        const_bin.push_back((bits >> 8) & 0xFF);
                    } else {
                        uint32_t bits; std::memcpy(&bits, &v, sizeof(float));
                        const_bin.push_back(bits & 0xFF); const_bin.push_back((bits >> 8) & 0xFF);
                        const_bin.push_back((bits >> 16) & 0xFF); const_bin.push_back((bits >> 24) & 0xFF);
                    }
                }
                // const 也需要支持地址偏移
                std::vector<uint8_t> const_padded = pad_data_to_addr(const_bin, ca);
                tools::HexConverter::write_memory_hex(para_config_.para_output_dir + fmt_hex(cb), const_padded);
                if (bank_names.count(cb)) bank_names[cb] += "/const2"; else bank_names[cb] = "const2";
                data_bank_set.insert(cb);
            }
        }
        
        // Compute max_cycles dynamically: for div, each step = iters*15+7 cycles
        int max_cycles = 50000;
        if (para_req.para_operator == "Div") {
            // 动态计算: 每 step = iters*15+7 cycles (+ bank conflict 额外 cycle)
            int div_iters = 3;  // default
            auto nit = para_req.para_parameters.find("newton_iters");
            if (nit != para_req.para_parameters.end() && !nit->second.para_data_i32.empty())
                div_iters = nit->second.para_data_i32[0];
            auto nit2 = para_req.para_parameters.find("iters");
            if (nit2 != para_req.para_parameters.end() && !nit2->second.para_data_i32.empty())
                div_iters = nit2->second.para_data_i32[0];
            int cycles_per_step = div_iters * 16 + 8 + 10;  // 16=15+1(conflict), 8=7+1, +10 margin
            max_cycles = output_max_addr * cycles_per_step + 200;
        }

        std::vector<int> data_banks_vec(data_bank_set.begin(), data_bank_set.end());

        // output_start_addr: 告诉仿真器输出数据从哪个 UR 地址开始
        int output_start_addr = xa;

        write_simulator_config(para_config_.para_output_dir, iq_init, 
                               data_banks_vec, xb, max_cycles, dtype_str, output_max_addr,
                               bank_names, output_start_addr, para_req.para_operator);
    }

    result.para_success = true;
    uint64_t total_inst_bytes = 0;
    for (const auto& f : iq_files) {
        total_inst_bytes += static_cast<uint64_t>(f.para_data.size());
    }
    result.para_total_insts = static_cast<uint32_t>(total_inst_bytes);
    result.para_total_elements = para_req.para_dimensions.para_n *
                                  para_req.para_dimensions.para_c *
                                  para_req.para_dimensions.para_h *
                                  para_req.para_dimensions.para_w;
    
    if (para_config_.para_verbose) {
        std::cout << "[TVGen] Generated files:\n";
        if (!result.para_iq_inst_paths.empty()) {
            std::cout << "  iq inst files: " << result.para_iq_inst_paths.size() << " files\n";
        }
        std::cout << "  input.bin: " << input_bin.size() << " bytes\n"
                  << "  other.bin: " << other_bin.size() << " bytes\n"
                  << "  alpha.bin: " << alpha_bin.size() << " bytes\n"
                  << "  golden.bin: " << golden_bin.size() << " bytes\n";
    }
    
    return result;
}

TvgenResult Tvgen::generate_from_json(const std::string& para_json_str) {
    AppJsonParser parser;
    AppOperatorRequest req = parser.parse_operator_request(para_json_str);
    return generate(req);
}

TvgenResult Tvgen::generate_from_file(const std::string& para_file_path) {
    AppJsonParser parser;
    AppOperatorRequest req = parser.parse_from_file(para_file_path);
    return generate(req);
}

std::string resolve_op_name(const std::string& alias) {
    if (alias == "add") return "FusedScaleAdd";
    if (alias == "mul") return "Mul";
    if (alias == "pow") return "PowTensorScalar";
    if (alias == "neg") return "neg";
    if (alias == "cos") return "cos";
    if (alias == "rsqrt") return "Rsqrt";
    if (alias == "div") return "Div";
    if (alias == "exp") return "Exp";
    if (alias == "log") return "Log";
    if (alias == "log1p") return "Log1p";
    if (alias == "bitwise_and" || alias == "bwa") return "BitwiseAnd";
    if (alias == "mean_dim" || alias == "mean") return "MeanDim";
    if (alias == "expand") return "Expand";
    //if (alias == "clone"     || alias == "cl")   return "Clone";
    //if (alias == "view")                           return "View";
    //if (alias == "cat")                            return "Cat";
    if (alias == "softmax") return "Softmax";
    return alias; // 默认返回原名
}

TvgenResult Tvgen::generate_random(uint32_t para_seed, 
                                   const std::string& para_operator, 
                                   const std::string& para_dtype,
                                   const std::string& para_layout) {
    // 1. 准备随机引擎
    std::mt19937 rng(para_seed);
    std::uniform_int_distribution<uint32_t> dim_dist(1, 8);
    // 默认数据分布 (-10.0 ~ 10.0)
    std::uniform_real_distribution<float> data_dist(-10.0f, 10.0f);

    // 2. 构造基础请求
    AppOperatorRequest req;
    
    // 随机维度 (W 固定为 4 对齐)
    req.para_dimensions.para_n = dim_dist(rng);
    req.para_dimensions.para_c = dim_dist(rng);
    req.para_dimensions.para_h = dim_dist(rng);
    req.para_dimensions.para_w = 4;

    // -------------------------------------------------------------
    // 【核心重构】不再需要 if-else 判断算子类型
    // -------------------------------------------------------------
    
    // 3. 获取算子对象
    std::string op_name;
    if (para_operator.empty()) {
        // 如果未指定算子，随机选一个
        auto all_ops = OpRegistry::get_instance().get_all_op_names();
        if (all_ops.empty()) return {false, "No operators registered!"};
        std::uniform_int_distribution<size_t> op_dist(0, all_ops.size() - 1);
        op_name = all_ops[op_dist(rng)];
    } else {
        op_name = resolve_op_name(para_operator);
    }

    OpBase* op = OpRegistry::get_instance().get_op(op_name);
    if (!op) {
        return {false, "Unknown operator: " + op_name};
    }
    
    // [BUG FIX] 在这里显式指定算子名 req.para_operator，以便下游生成仿真器配置时能输出正确的名字。
    // 如果不指定，模拟器在读取配置时会丢失算子名从而导致崩溃。
    req.para_operator = op_name;

    // 4. 设置内存布局模式
    if (!para_layout.empty()) {
        req.para_layout_mode = para_layout;
    }

    // 4a. bank_conflict 模式下每个 tensor 独立随机选择 bank (0-7)
    // [BUG FIX] 添加了对应的 bank_conflict_1/2/3 取值判断，以保证结构化的冲突测例输入能正常工作。
    //     bank_conflict_4 模式下 4 个 tensor 全部放 bank 0（固定 4 冲突测试）
    if (para_layout == "bank_conflict" || para_layout == "bank_conflict_1" ||
        para_layout == "bank_conflict_2" || para_layout == "bank_conflict_3" ||
        para_layout == "bank_conflict_4" || para_layout == "bank_conflict_5") {
        if (para_layout == "bank_conflict_4") {
            // 固定 4 冲突：input/other/alpha/output 全在 bank 0
            req.para_bc_input_bank  = 0;
            req.para_bc_other_bank  = 0;
            req.para_bc_alpha_bank  = 0;
            req.para_bc_output_bank = 0;
        } else if (para_layout == "bank_conflict_1") {
            // Input+Other 同 bank 0, Output 在 bank 1
            req.para_bc_input_bank  = 0;
            req.para_bc_other_bank  = 0;
            req.para_bc_alpha_bank  = 2;
            req.para_bc_output_bank = 1;
        } else if (para_layout == "bank_conflict_2") {
            // Input+Output 同 bank 0, Other 在 bank 1
            req.para_bc_input_bank  = 0;
            req.para_bc_other_bank  = 1;
            req.para_bc_alpha_bank  = 2;
            req.para_bc_output_bank = 0;
        } else if (para_layout == "bank_conflict_3") {
            // Other+Output 同 bank 0, Input 在 bank 1
            req.para_bc_input_bank  = 1;
            req.para_bc_other_bank  = 0;
            req.para_bc_alpha_bank  = 2;
            req.para_bc_output_bank = 0;
        } else if (para_layout == "bank_conflict_5") {
            // Other+Alpha 同 bank 1 (FPMUL 两个输入冲突)
            req.para_bc_input_bank  = 0;
            req.para_bc_other_bank  = 1;
            req.para_bc_alpha_bank  = 1;
            req.para_bc_output_bank = 2;
        } else {
            std::uniform_int_distribution<int> bank_dist(0, 7);
            
            auto it_i = para_config_.para_int_overrides.find("input_bank");
            req.para_bc_input_bank = (it_i != para_config_.para_int_overrides.end()) ? it_i->second[0] : bank_dist(rng);
            
            auto it_o = para_config_.para_int_overrides.find("other_bank");
            req.para_bc_other_bank = (it_o != para_config_.para_int_overrides.end()) ? it_o->second[0] : bank_dist(rng);
            
            auto it_a = para_config_.para_int_overrides.find("alpha_bank");
            req.para_bc_alpha_bank = (it_a != para_config_.para_int_overrides.end()) ? it_a->second[0] : bank_dist(rng);
            
            auto it_out = para_config_.para_int_overrides.find("output_bank");
            req.para_bc_output_bank = (it_out != para_config_.para_int_overrides.end()) ? it_out->second[0] : bank_dist(rng);
        }
        std::cout << "[Info] Bank conflict layout: input=bank" << req.para_bc_input_bank
                  << ", other=bank" << req.para_bc_other_bank
                  << ", alpha=bank" << req.para_bc_alpha_bank
                  << ", output=bank" << req.para_bc_output_bank << "\n";
        // Normalize layout mode for downstream code
        req.para_layout_mode = "bank_conflict";
    }

    // 4b. For bank_conflict: cap dimensions so all tensors fit within 256 addresses
    //     per bank.  Compute per-bank usage based on actual tensor→bank mapping.
    if (req.para_layout_mode == "bank_conflict") {
        uint32_t elems_per_word = (para_dtype == "fp16") ? 8 : 4;
        // Count per-bank: full-size tensors (input/other/output each need words_needed)
        //                 and alpha (1 word)
        std::map<int, int> bank_full;   // bank -> count of full-size tensors
        std::map<int, int> bank_alpha;  // bank -> count of alpha tensors
        bank_full[req.para_bc_input_bank]++;
        bank_full[req.para_bc_other_bank]++;
        bank_full[req.para_bc_output_bank]++;
        bank_alpha[req.para_bc_alpha_bank]++;
        // For each bank: full_count * words_needed + alpha_count <= 256
        // => words_needed <= (256 - alpha_count) / full_count
        uint32_t max_words = 256;
        for (auto& [bank, fc] : bank_full) {
            int ac = bank_alpha.count(bank) ? bank_alpha[bank] : 0;
            if (fc > 0) {
                uint32_t mw = static_cast<uint32_t>((256 - ac) / fc);
                if (mw < max_words) max_words = mw;
            }
        }
        uint32_t max_total = max_words * elems_per_word;
        while (req.para_dimensions.para_n * req.para_dimensions.para_c *
               req.para_dimensions.para_h * req.para_dimensions.para_w > max_total) {
            if (req.para_dimensions.para_n > 1)      req.para_dimensions.para_n--;
            else if (req.para_dimensions.para_c > 1)  req.para_dimensions.para_c--;
            else if (req.para_dimensions.para_h > 1)  req.para_dimensions.para_h--;
            else break;
        }
    }

    // 4c. 默认模式下也约束总 word 数不超过 256（UR 地址 8-bit 上限）
    if (req.para_layout_mode != "bank_conflict") {
        uint32_t elems_per_word = (para_dtype == "fp16") ? 8 : 4;
        uint32_t max_total = 256 * elems_per_word;
        while (req.para_dimensions.para_n * req.para_dimensions.para_c *
               req.para_dimensions.para_h * req.para_dimensions.para_w > max_total) {
            if (req.para_dimensions.para_n > 1)      req.para_dimensions.para_n--;
            else if (req.para_dimensions.para_c > 1)  req.para_dimensions.para_c--;
            else if (req.para_dimensions.para_h > 1)  req.para_dimensions.para_h--;
            else break;
        }
    }

    // 合并命令行覆盖的 int 参数（如 --newton-iters，以及 bank 设置）
    // 必须放在 op->fill_random_request 之前！
    for (const auto& kv : para_config_.para_int_overrides) {
        app::AppTensorDesc desc;
        desc.para_name = kv.first;
        desc.para_dtype = app::AppDataType::INT32;
        desc.para_shape = {static_cast<uint32_t>(kv.second.size())};
        desc.para_data_i32 = kv.second;
        req.para_parameters[kv.first] = desc;
    }

    // 5. 【多态调用】让算子自己去造数据
    // 这一行代码替代了原来的几百行逻辑
    // 传入 para_dtype 字符串 ("fp16"/"fp32")，算子自己解析
    op->fill_random_request(req, rng, data_dist, para_dtype);

    // -------------------------------------------------------------

    // 5. 打印通用日志
    if (para_config_.para_verbose) {
        uint32_t total = req.para_dimensions.para_n * req.para_dimensions.para_c *
                         req.para_dimensions.para_h * req.para_dimensions.para_w;
        std::cout << "[TVGen] Random test for [" << op_name << "] with seed=" << para_seed 
                  << ", dtype=" << para_dtype << "\n"
                  << "  Dimensions: N=" << req.para_dimensions.para_n
                  << ", C=" << req.para_dimensions.para_c
                  << ", H=" << req.para_dimensions.para_h
                  << ", W=" << req.para_dimensions.para_w
                  << " (Total=" << total << ")\n";
    }
    
    // 6. 进入标准处理流程
    return generate(req);
}

// ==========================================
// 文件写入
// ==========================================

bool Tvgen::write_bin_file(const std::string& para_path, const std::vector<uint8_t>& para_data) {
    std::ofstream file(para_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(para_data.data()), para_data.size());
    return true;
}

bool Tvgen::write_hex_file(const std::string& para_path, const std::vector<uint8_t>& para_data) {
    std::ofstream file(para_path);
    if (!file.is_open()) {
        return false;
    }
    
    file << std::hex << std::setfill('0');
    for (size_t i = 0; i < para_data.size(); ++i) {
        file << std::setw(2) << static_cast<int>(para_data[i]);
        if ((i + 1) % 16 == 0) {
            file << "\n";
        } else {
            file << " ";
        }
    }
    
    return true;
}
