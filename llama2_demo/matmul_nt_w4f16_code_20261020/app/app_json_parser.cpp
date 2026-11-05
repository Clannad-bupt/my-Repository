#include "app_json_parser.h"

/**
 * @file app_json_parser.cpp
 * @brief App 层 JSON 解析器实现
 */

#include <fstream>
#include <sstream>
#include <iostream>
#include <cctype>
#include <cmath>
#include <algorithm>

namespace aiacc {
namespace app {

// ==========================================
// 数据类型转换
// ==========================================

AppDataType AppJsonParser::parse_dtype(const std::string& para_dtype_str) {
    if (para_dtype_str == "float32" || para_dtype_str == "fp32") {
        return AppDataType::FLOAT32;
    } else if (para_dtype_str == "int32") {
        return AppDataType::INT32;
    } else if (para_dtype_str == "float16" || para_dtype_str == "fp16") {
        return AppDataType::FLOAT16;
    } else if (para_dtype_str == "int4") {
        return AppDataType::INT4;
    }
    return AppDataType::UNKNOWN;
}

std::string AppJsonParser::dtype_to_string(AppDataType para_dtype) {
    switch (para_dtype) {
        case AppDataType::FLOAT32: return "float32";
        case AppDataType::INT32: return "int32";
        case AppDataType::FLOAT16: return "float16";
        case AppDataType::INT4: return "int4";
        default: return "unknown";
    }
}

// ==========================================
// 基础解析辅助函数
// ==========================================

void AppJsonParser::skip_whitespace() {
    while (para_pos_ < para_json_.size() && std::isspace(para_json_[para_pos_])) {
        ++para_pos_;
    }
}

char AppJsonParser::peek_char() {
    skip_whitespace();
    if (para_pos_ >= para_json_.size()) {
        throw std::runtime_error("Unexpected end of JSON");
    }
    return para_json_[para_pos_];
}

char AppJsonParser::consume_char() {
    skip_whitespace();
    if (para_pos_ >= para_json_.size()) {
        throw std::runtime_error("Unexpected end of JSON");
    }
    return para_json_[para_pos_++];
}

bool AppJsonParser::match_char(char para_expected) {
    skip_whitespace();
    if (para_pos_ < para_json_.size() && para_json_[para_pos_] == para_expected) {
        ++para_pos_;
        return true;
    }
    return false;
}

std::string AppJsonParser::parse_string() {
    skip_whitespace();
    if (consume_char() != '"') {
        throw std::runtime_error("Expected '\"' at start of string");
    }
    
    std::string result;
    while (para_pos_ < para_json_.size()) {
        char c = para_json_[para_pos_++];
        if (c == '"') {
            return result;
        } else if (c == '\\' && para_pos_ < para_json_.size()) {
            char next = para_json_[para_pos_++];
            switch (next) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                default: result += next; break;
            }
        } else {
            result += c;
        }
    }
    throw std::runtime_error("Unterminated string");
}

double AppJsonParser::parse_number() {
    skip_whitespace();
    size_t start = para_pos_;
    
    // 处理负号
    if (para_pos_ < para_json_.size() && para_json_[para_pos_] == '-') {
        ++para_pos_;
    }
    
    // 整数部分
    while (para_pos_ < para_json_.size() && std::isdigit(para_json_[para_pos_])) {
        ++para_pos_;
    }
    
    // 小数部分
    if (para_pos_ < para_json_.size() && para_json_[para_pos_] == '.') {
        ++para_pos_;
        while (para_pos_ < para_json_.size() && std::isdigit(para_json_[para_pos_])) {
            ++para_pos_;
        }
    }
    
    // 指数部分
    if (para_pos_ < para_json_.size() && (para_json_[para_pos_] == 'e' || para_json_[para_pos_] == 'E')) {
        ++para_pos_;
        if (para_pos_ < para_json_.size() && (para_json_[para_pos_] == '+' || para_json_[para_pos_] == '-')) {
            ++para_pos_;
        }
        while (para_pos_ < para_json_.size() && std::isdigit(para_json_[para_pos_])) {
            ++para_pos_;
        }
    }
    
    if (start == para_pos_) {
        throw std::runtime_error("Expected number");
    }
    
    return std::stod(para_json_.substr(start, para_pos_ - start));
}

std::vector<double> AppJsonParser::parse_number_array() {
    std::vector<double> result;
    parse_array_start();
    
    while (!match_char(']')) {
        if (!result.empty()) {
            if (!match_char(',')) {
                throw std::runtime_error("Expected ',' in array");
            }
        }
        
        // 跳过字符串元素（如 "...省略..."）
        skip_whitespace();
        if (peek_char() == '"') {
            parse_string();  // 跳过字符串
            continue;
        }
        
        result.push_back(parse_number());
    }
    
    return result;
}

std::vector<uint32_t> AppJsonParser::parse_uint_array() {
    std::vector<uint32_t> result;
    parse_array_start();
    
    while (!match_char(']')) {
        if (!result.empty()) {
            if (!match_char(',')) {
                throw std::runtime_error("Expected ',' in array");
            }
        }
        result.push_back(static_cast<uint32_t>(parse_number()));
    }
    
    return result;
}

void AppJsonParser::parse_object_start() {
    if (!match_char('{')) {
        throw std::runtime_error("Expected '{'");
    }
}

void AppJsonParser::parse_object_end() {
    if (!match_char('}')) {
        throw std::runtime_error("Expected '}'");
    }
}

bool AppJsonParser::parse_object_has_more() {
    skip_whitespace();
    if (peek_char() == '}') {
        return false;
    }
    return true;
}

std::string AppJsonParser::parse_object_key() {
    std::string key = parse_string();
    if (!match_char(':')) {
        throw std::runtime_error("Expected ':' after object key");
    }
    return key;
}

void AppJsonParser::parse_array_start() {
    if (!match_char('[')) {
        throw std::runtime_error("Expected '['");
    }
}

void AppJsonParser::parse_array_end() {
    if (!match_char(']')) {
        throw std::runtime_error("Expected ']'");
    }
}

bool AppJsonParser::parse_array_has_more() {
    skip_whitespace();
    if (peek_char() == ']') {
        return false;
    }
    return true;
}

// ==========================================
// 高级解析函数
// ==========================================

AppTensorDesc AppJsonParser::parse_tensor_desc(const std::string& para_name) {
    AppTensorDesc desc;
    desc.para_name = para_name;
    
    parse_object_start();
    bool first = true;
    
    while (parse_object_has_more()) {
        if (!first) {
            if (!match_char(',')) {
                throw std::runtime_error("Expected ',' in tensor object");
            }
        }
        first = false;
        
        std::string key = parse_object_key();
        
        if (key == "shape") {
            desc.para_shape = parse_uint_array();
        } else if (key == "dtype") {
            desc.para_dtype = parse_dtype(parse_string());
        } else if (key == "data") {
            std::vector<double> data = parse_number_array();
            if (desc.para_dtype == AppDataType::INT32) {
                desc.para_data_i32.reserve(data.size());
                for (double v : data) {
                    desc.para_data_i32.push_back(static_cast<int32_t>(v));
                }
            } else {
                desc.para_data_f32.reserve(data.size());
                for (double v : data) {
                    desc.para_data_f32.push_back(static_cast<float>(v));
                }
            }
        } else {
            // 跳过未知字段
            skip_whitespace();
            char c = peek_char();
            if (c == '"') {
                parse_string();
            } else if (c == '[') {
                parse_number_array();
            } else if (c == '{') {
                // 跳过嵌套对象
                int depth = 1;
                ++para_pos_;
                while (depth > 0 && para_pos_ < para_json_.size()) {
                    if (para_json_[para_pos_] == '{') ++depth;
                    else if (para_json_[para_pos_] == '}') --depth;
                    ++para_pos_;
                }
            } else {
                parse_number();
            }
        }
    }
    
    parse_object_end();
    return desc;
}

AppDimensions AppJsonParser::parse_dimensions() {
    AppDimensions dims;
    
    parse_object_start();
    bool first = true;
    
    while (parse_object_has_more()) {
        if (!first) {
            if (!match_char(',')) {
                throw std::runtime_error("Expected ',' in dimensions object");
            }
        }
        first = false;
        
        std::string key = parse_object_key();
        uint32_t value = static_cast<uint32_t>(parse_number());
        
        if (key == "N" || key == "n") {
            dims.para_n = value;
        } else if (key == "C" || key == "c") {
            dims.para_c = value;
        } else if (key == "H" || key == "h") {
            dims.para_h = value;
        } else if (key == "W" || key == "w") {
            dims.para_w = value;
        }
    }
    
    parse_object_end();
    return dims;
}

std::map<std::string, AppTensorDesc> AppJsonParser::parse_tensor_map() {
    std::map<std::string, AppTensorDesc> result;
    
    parse_object_start();
    bool first = true;
    
    while (parse_object_has_more()) {
        if (!first) {
            if (!match_char(',')) {
                throw std::runtime_error("Expected ',' in tensor map");
            }
        }
        first = false;
        
        std::string name = parse_object_key();
        result[name] = parse_tensor_desc(name);
    }
    
    parse_object_end();
    return result;
}

// ==========================================
// 主解析函数
// ==========================================

AppOperatorRequest AppJsonParser::parse_operator_request(const std::string& para_json_str) {
    para_json_ = para_json_str;
    para_pos_ = 0;
    
    AppOperatorRequest req;
    
    parse_object_start();
    bool first = true;
    
    while (parse_object_has_more()) {
        if (!first) {
            if (!match_char(',')) {
                throw std::runtime_error("Expected ',' in root object");
            }
        }
        first = false;
        
        std::string key = parse_object_key();
        
        if (key == "operator") {
            req.para_operator = parse_string();
        } else if (key == "dimensions") {
            req.para_dimensions = parse_dimensions();
        } else if (key == "parameters") {
            req.para_parameters = parse_tensor_map();
        } else if (key == "inputs") {
            req.para_inputs = parse_tensor_map();
        } else if (key == "outputs") {
            req.para_outputs = parse_tensor_map();
        } else if (key == "layout_mode") {
            // 可选：Add 等算子用 "default" 或 "bank_conflict" 切换 UR 布局
            req.para_layout_mode = parse_string();
        } else {
            // 跳过未知字段
            skip_whitespace();
            char c = peek_char();
            if (c == '"') {
                parse_string();
            } else if (c == '[') {
                parse_number_array();
            } else if (c == '{') {
                int depth = 1;
                ++para_pos_;
                while (depth > 0 && para_pos_ < para_json_.size()) {
                    if (para_json_[para_pos_] == '{') ++depth;
                    else if (para_json_[para_pos_] == '}') --depth;
                    ++para_pos_;
                }
            } else {
                parse_number();
            }
        }
    }
    
    parse_object_end();
    return req;
}

AppOperatorRequest AppJsonParser::parse_from_file(const std::string& para_file_path) {
    std::ifstream file(para_file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + para_file_path);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_operator_request(buffer.str());
}

// ==========================================
// 辅助函数实现
// ==========================================

void print_operator_request(const AppOperatorRequest& para_req) {
    std::cout << "=== Operator Request ===" << std::endl;
    std::cout << "Operator: " << para_req.para_operator << std::endl;
    std::cout << "Dimensions: N=" << para_req.para_dimensions.para_n
              << ", C=" << para_req.para_dimensions.para_c
              << ", H=" << para_req.para_dimensions.para_h
              << ", W=" << para_req.para_dimensions.para_w << std::endl;
    
    std::cout << "Parameters:" << std::endl;
    for (const auto& kv : para_req.para_parameters) {
        std::cout << "  " << kv.first << ": shape=[";
        for (size_t i = 0; i < kv.second.para_shape.size(); ++i) {
            if (i > 0) std::cout << ",";
            std::cout << kv.second.para_shape[i];
        }
        std::cout << "], dtype=" << AppJsonParser::dtype_to_string(kv.second.para_dtype);
        std::cout << ", data_count=" << (kv.second.para_data_f32.size() + kv.second.para_data_i32.size());
        std::cout << std::endl;
    }
    
    std::cout << "Inputs:" << std::endl;
    for (const auto& kv : para_req.para_inputs) {
        std::cout << "  " << kv.first << ": shape=[";
        for (size_t i = 0; i < kv.second.para_shape.size(); ++i) {
            if (i > 0) std::cout << ",";
            std::cout << kv.second.para_shape[i];
        }
        std::cout << "], dtype=" << AppJsonParser::dtype_to_string(kv.second.para_dtype);
        std::cout << ", data_count=" << (kv.second.para_data_f32.size() + kv.second.para_data_i32.size());
        std::cout << std::endl;
    }
}

std::string validate_operator_request(const AppOperatorRequest& para_req) {
    // 检查算子名称
    if (para_req.para_operator.empty()) {
        return "Operator name is empty";
    }
    
    // 检查维度
    if (para_req.para_dimensions.para_n == 0 ||
        para_req.para_dimensions.para_c == 0 ||
        para_req.para_dimensions.para_h == 0 ||
        para_req.para_dimensions.para_w == 0) {
        return "Dimensions cannot be zero";
    }
    
    // 检查输入张量
    if (para_req.para_inputs.empty()) {
        return "No input tensors specified";
    }
    
    // 检查数据类型
    for (const auto& kv : para_req.para_inputs) {
        if (kv.second.para_dtype == AppDataType::UNKNOWN) {
            return "Unknown dtype for input: " + kv.first;
        }
    }
    
    return "";  // 空字符串表示验证通过
}

} // namespace app
} // namespace aiacc
