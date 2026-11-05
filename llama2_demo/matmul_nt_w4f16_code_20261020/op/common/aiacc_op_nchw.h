#ifndef AIACC_OP_NCHW_H
#define AIACC_OP_NCHW_H

/**
 * @file aiacc_op_nchw.h
 * @brief OP 层公共工具：NCHW 索引/形状相关的辅助函数（供算子 emit 做地址/循环展开）。
 */

#include <cstdint>

namespace aiacc {
namespace op {

/**
 * @brief 线性下标 -> NCHW 四维下标
 * 约定：shape 均为正。
 */
struct NchwIndex {
    int64_t n;
    int64_t c;
    int64_t h;
    int64_t w;
};

inline NchwIndex linear_to_nchw(int64_t linear_idx, uint32_t n, uint32_t c, uint32_t h, uint32_t w) {
    const int64_t chw = (int64_t)c * (int64_t)h * (int64_t)w;
    const int64_t hw = (int64_t)h * (int64_t)w;

    const int64_t n_idx = linear_idx / chw;
    const int64_t rem1 = linear_idx % chw;
    const int64_t c_idx = rem1 / hw;
    const int64_t rem2 = rem1 % hw;
    const int64_t h_idx = rem2 / (int64_t)w;
    const int64_t w_idx = rem2 % (int64_t)w;

    return NchwIndex{n_idx, c_idx, h_idx, w_idx};
}

} // namespace op
} // namespace aiacc

#endif
