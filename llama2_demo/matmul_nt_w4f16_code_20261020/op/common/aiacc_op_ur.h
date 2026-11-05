#ifndef AIACC_OP_UR_H
#define AIACC_OP_UR_H

/**
 * @file aiacc_op_ur.h
 * @brief OP 层公共工具：UR 地址与 stride/broadcast 约束的检查与计算（可写入软件 sink 便于单测）。
 */

#include <cstdint>
#include <stdexcept>
#include <string>

namespace aiacc {
namespace op {

// 说明：这里放“所有算子都会用到”的 UR 地址/stride 公共逻辑。
// - UR 在当前 RTL 中每个地址存储 128bit (=16B)
// - stride 定义为“元素间隔数”

/**
 * @brief 软件侧可观察的 UR 地址
 * bank: UR bank 编号
 * addr_word: bank 内地址（以 128bit/addr 为单位的 word 地址）
 */
struct UrAddr {
    uint32_t bank;
    uint32_t addr_word;
};

inline uint32_t ur_word_bytes() {
    return 16; // 128bit
}

inline uint32_t ur_row_elems_from_elem_bytes(uint32_t elem_bytes) {
    if (elem_bytes == 0 || (ur_word_bytes() % elem_bytes) != 0) {
        throw std::runtime_error("unsupported elem bytes for UR packing");
    }
    return ur_word_bytes() / elem_bytes;
}

/**
 * @brief 检查 stride 是否满足 UR 行对齐约束
 * 约束：假设 UR 一行元素数为 X，则 N/C/H stride 必须是 X 的整数倍。
 * stride=0 表示广播 (broadcast)，允许不对齐检查。
 */
inline void validate_stride_row_alignment_or_throw(
    int32_t stride_n,
    int32_t stride_c,
    int32_t stride_h,
    uint32_t ur_row_elems,
    const char* tensor_name) {

    auto check_one = [&](int32_t stride, const char* dim) {
        if (stride == 0) return;
        const int32_t mod = stride % (int32_t)ur_row_elems;
        if (mod != 0) {
            throw std::runtime_error(std::string(tensor_name) + "." + dim + " stride must be multiple of X (UR row elems)");
        }
    };

    check_one(stride_n, "stride_n");
    check_one(stride_c, "stride_c");
    check_one(stride_h, "stride_h");
}

} // namespace op
} // namespace aiacc

#endif
