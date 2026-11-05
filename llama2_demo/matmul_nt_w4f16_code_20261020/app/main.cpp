#include "tvgen.h"
#include "app_json_parser.h"

/**
 * @file main.cpp
 * @brief TVGen 命令行入口
 *
 * 用法：
 *   ./tvgen --json <json_file> --output <output_dir>
 *   ./tvgen --json-str '<json_string>'
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>

void print_usage(const char* prog_name) {
    std::cout << "TVGen - Test Vector Generator for SMI_IQ\n\n"
              << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  --json <file>       Load operator from JSON file\n"
              << "  --json-str <str>    Parse operator from JSON string\n"
              << "  --random            Generate random test case\n"
              << "  --seed <num>        Random seed (default: time-based)\n"
              << "  --operator <op>     Operator type (add/pow/mul/neg/rsqrt/div/exp/log/log1p/softmax)\n"
              << "  --dtype <dtype>     Data type (fp32/fp16)\n"
              << "  --output <dir>      Output directory (default: out)\n"
              << "  --layout <layout>   Memory layout (default/bank_conflict)\n"
              << "  --verbose           Print detailed information\n"
              << "  --newton-iters <N>  [Div] Newton iteration count 1~8 (random mode)\n"
              << "  --help              Show this help\n\n"
              << "Output files:\n"
              << "  instructions/         Micro-instructions per IQ (Compiler)\n"
              << "  input.bin             Input tensor data (DataGen)\n"
              << "  other.bin             Other tensor data (DataGen)\n"
              << "  alpha.bin             Alpha parameter data (DataGen)\n"
              << "  golden.bin            Expected output (GoldenCalc)\n";
}

int main(int argc, char** argv) {
    std::string json_file;
    std::string json_str;
    std::string output_dir = "out";
    bool verbose = false;
    bool random_mode = false;
    uint32_t seed = static_cast<uint32_t>(std::time(nullptr));
    std::string operator_type; // 添加算子类型参数
    std::string dtype; // 添加数据类型参数
    std::string layout; // 内存布局模式
    int32_t newton_iters = -1;  // -1 表示未指定，使用默认
    std::map<std::string, std::vector<int>> int_overrides;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--json" && i + 1 < argc) {
            json_file = argv[++i];
        } else if (arg == "--json-str" && i + 1 < argc) {
            json_str = argv[++i];
        } else if (arg == "--random") {
            random_mode = true;
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--output" && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--operator" && i + 1 < argc) {
            operator_type = argv[++i]; // 添加--operator参数解析
        } else if (arg == "--dtype" && i + 1 < argc) {
            dtype = argv[++i]; // 添加--dtype参数解析
        } else if (arg == "--layout" && i + 1 < argc) {
            layout = argv[++i];
        } else if (arg == "--newton-iters" && i + 1 < argc) {
            newton_iters = std::stoi(argv[++i]); // 添加--newton-iters参数解析
        } else if (arg == "--input-bank" && i + 1 < argc) {
            int_overrides["input_bank"] = {std::stoi(argv[++i])};
        } else if (arg == "--other-bank" && i + 1 < argc) {
            int_overrides["other_bank"] = {std::stoi(argv[++i])};
        } else if (arg == "--alpha-bank" && i + 1 < argc) {
            int_overrides["alpha_bank"] = {std::stoi(argv[++i])};
        } else if (arg == "--const2-bank" && i + 1 < argc) {
            int_overrides["const2_bank"] = {std::stoi(argv[++i])};
        } else if (arg == "--output-bank" && i + 1 < argc) {
            int_overrides["output_bank"] = {std::stoi(argv[++i])};
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // 检查输入
    if (json_file.empty() && json_str.empty() && !random_mode) {
        std::cerr << "Error: Must specify --json, --json-str, or --random\n\n";
        print_usage(argv[0]);
        return 1;
    }
    
    try {
        // 配置 TVGen
        aiacc::app::TvgenConfig config;
        config.para_output_dir = output_dir;
        config.para_verbose = verbose;
        config.para_int_overrides = int_overrides;
        if (newton_iters >= 0) {
            if (newton_iters < 1) newton_iters = 1;
            if (newton_iters > 8) newton_iters = 8;
            config.para_int_overrides["newton_iters"] = {newton_iters};
        }
        
        aiacc::app::Tvgen tvgen(config);
        aiacc::app::TvgenResult result;
        
        // 执行生成
        if (random_mode) {
            if (verbose) {
                std::cout << "[TVGen] Random mode with seed=" << seed << "\n";
            }
            // 使用 --operator 和 --dtype 参数调用 generate_random
            result = tvgen.generate_random(seed, operator_type, dtype, layout);
        } else if (!json_file.empty()) {
            if (verbose) {
                std::cout << "[TVGen] Loading JSON from: " << json_file << "\n";
            }
            result = tvgen.generate_from_file(json_file);
        } else {
            if (verbose) {
                std::cout << "[TVGen] Parsing JSON string\n";
            }
            result = tvgen.generate_from_json(json_str);
        }
        
        // 输出结果
        if (result.para_success) {
            std::cout << "[OK] TVGen completed successfully\n"
                      << "  Output directory: " << output_dir << "\n"
                      << "  Total instructions: " << result.para_total_insts << " bytes\n"
                      << "  Total elements: " << result.para_total_elements << "\n"
                      << "\nGenerated files:\n";
            
            // 只显示实际生成的文件
            if (!result.para_iq_inst_paths.empty()) {
                std::cout << "  instructions/ (" << result.para_iq_inst_paths.size() << " IQ files)\n";
            }
            std::cout << "  " << result.para_input_bin_path << "\n"
                      << "  " << result.para_other_bin_path << "\n"
                      << "  " << result.para_alpha_bin_path << "\n"
                      << "  " << result.para_golden_bin_path << "\n";
            if (!result.para_metadata_path.empty()) {
                std::cout << "  " << result.para_metadata_path << "\n";
            }
            return 0;
        } else {
            std::cerr << "[ERROR] TVGen failed: " << result.para_error_msg << "\n";
            return 2;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception: " << e.what() << "\n";
        return 3;
    }
}
