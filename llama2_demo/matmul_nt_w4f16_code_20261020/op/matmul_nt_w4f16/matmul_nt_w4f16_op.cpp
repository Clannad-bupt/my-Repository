#include "matmul_nt_w4f16_op.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <stdexcept>

#include "../../app/fp16_utils.h"
#include "../../iq/modules/fpmul_iq.h"
#include "../../iq/modules/fp16mul_iq.h"
#include "../../iq/modules/fp32addtree5to1_iq.h"
#include "../../iq/modules/fp32addtree8to1_iq.h"
#include "../../iq/modules/intadd_iq.h"
#include "../../iq/modules/int8tofp16_iq.h"
#include "../../iq/modules/fptofp_iq.h"
#include "../../iq/iq_id_defs.h"
#include "../../iq/modules/shiftdown_iq.h"
#include "../../iq/modules/shiftup_iq.h"
#include "../../iq/modules/smi_ds_t_iq.h"
#include "../../iq/modules/fpadd_iq.h"
#include "../../iq/modules/ur_iq.h"
#include "ur_code_extractor.h"

namespace aiacc {
namespace op {

namespace {

template <typename T>
T get_i32_param(const app::AppOperatorRequest& req, const std::string& key, T def) {
    const auto it = req.para_parameters.find(key);
    if (it == req.para_parameters.end() || it->second.para_data_i32.empty()) return def;
    return static_cast<T>(it->second.para_data_i32[0]);
}

void append_u32_le(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}

void append_fp16_le(std::vector<uint8_t>& out, float v) {
    const uint16_t h = app::float32_to_float16(v);
    out.push_back(static_cast<uint8_t>(h & 0xFFu));
    out.push_back(static_cast<uint8_t>((h >> 8) & 0xFFu));
}

void append_fp32_le(std::vector<uint8_t>& out, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(float));
    append_u32_le(out, bits);
}

std::vector<float> build_fixed_a_8x8() {
    constexpr size_t kDim = 64u;
    std::vector<float> a(kDim * kDim, 0.0f);
    for (size_t r = 0; r < kDim; ++r) {
        for (size_t c = 0; c < kDim; ++c) {
            // 追踪位型：mxny（m=行低4bit, x=行高4bit, n=列低4bit, y=列高4bit）。
            // 行列按1开始计数；超过16时自动向 x/y 进位。
            const uint16_t row = static_cast<uint16_t>(r + 1u);
            const uint16_t col = static_cast<uint16_t>(c + 1u);
            const uint16_t m = static_cast<uint16_t>(row & 0xFu);
            const uint16_t x = static_cast<uint16_t>((row >> 4) & 0xFu);
            const uint16_t n = static_cast<uint16_t>(col & 0xFu);
            const uint16_t y = static_cast<uint16_t>((col >> 4) & 0xFu);
            const uint16_t h = static_cast<uint16_t>((m << 12) | (x << 8) | (n << 4) | y);
            a[r * kDim + c] = app::float16_to_float32(h);
        }
    }
    return a;
}

std::vector<float> build_fixed_b_8x8() {
    constexpr size_t kDim = 64u;
    std::vector<float> b(kDim * kDim, 0.0f);
    for (size_t r = 0; r < kDim; ++r) {
        for (size_t c = 0; c < kDim; ++c) {
            // B 采用反向编码：按 (col,row) 组织 mxny，便于核对 A[i,k] 与 B[k,j] 同块对齐。
            const uint16_t row = static_cast<uint16_t>(c + 1u);
            const uint16_t col = static_cast<uint16_t>(r + 1u);
            const uint16_t m = static_cast<uint16_t>(row & 0xFu);
            const uint16_t x = static_cast<uint16_t>((row >> 4) & 0xFu);
            const uint16_t n = static_cast<uint16_t>(col & 0xFu);
            const uint16_t y = static_cast<uint16_t>((col >> 4) & 0xFu);
            const uint16_t h = static_cast<uint16_t>((m << 12) | (x << 8) | (n << 4) | y);
            b[r * kDim + c] = app::float16_to_float32(h);
        }
    }
    return b;
}

// 按“先转置，再间插=2”生成 A 侧存储（8x8）：
// 设 A^T 的两行为 t, t+1（t 为偶数），每行 8 元素；
// 每个 128b word 存 [A^T[t][0..3], A^T[t+1][0..3]]，下一个 word 存 [A^T[t][4..7], A^T[t+1][4..7]]。
// k_block=0 → A[:,0:32]；k_block=1 → A[:,32:64]；每块 8×32 tile，transpose+interleave=2。
void pack_a_transpose_interleave2_kchunk(const std::vector<float>& a,
                                         std::vector<uint8_t>& out,
                                         uint32_t k_block) {
    out.clear();
    constexpr size_t kGlobalDim = 64u;
    constexpr size_t kTileRows = 8u;
    constexpr uint32_t kKChunk = 32u;
    constexpr size_t kTileCols = 32u;
    if (a.size() < kGlobalDim * kGlobalDim) return;
    const size_t k_col_base = static_cast<size_t>(k_block) * static_cast<size_t>(kKChunk);
    if (k_col_base + kTileCols > kGlobalDim) return;
    for (size_t t_local = 0; t_local < kTileCols; t_local += 2u) {
        const size_t k_col = k_col_base + t_local;
        for (size_t row_blk = 0; row_blk < (kTileRows / 4u); ++row_blk) {
            const size_t r0 = row_blk * 4u;
            for (int i = 0; i < 4; ++i) {
                const size_t src_row = r0 + static_cast<size_t>(i);
                append_fp16_le(out, a[src_row * kGlobalDim + k_col]);
            }
            for (int i = 0; i < 4; ++i) {
                const size_t src_row = r0 + static_cast<size_t>(i);
                append_fp16_le(out, a[src_row * kGlobalDim + (k_col + 1u)]);
            }
        }
    }
}

void pack_a_transpose_interleave2_8x8(const std::vector<float>& a, std::vector<uint8_t>& out) {
    pack_a_transpose_interleave2_kchunk(a, out, 0u);
}

constexpr uint32_t kMagicMsh5 = 0x3548534Du;  // 'MSH5' — 8 SMC A/B 分块描述（other.bin）
constexpr uint32_t kMagicZp8 = 0x38505A4Du;   // 'MZP8' — optional ZP(int8-expanded) trailer
constexpr uint32_t kMagicSc16 = 0x36314353u;  // 'SC16' — optional Scale(fp16 template) trailer

uint8_t clamp_u4_from_i32(int32_t v) {
    if (v < 0) return 0u;
    if (v > 15) return 15u;
    return static_cast<uint8_t>(v);
}

uint8_t clamp_u4_from_f32(float v) {
    const int32_t q = static_cast<int32_t>(std::lround(v));
    return clamp_u4_from_i32(q);
}

uint8_t extract_u4_from_fp16_bits(float v) {
    // 回退路径：若未提供 int4 张量，则从 fp16 位型低 4bit 提取，避免 round 导致全 0。
    const uint16_t h = app::float32_to_float16(v);
    return static_cast<uint8_t>(h & 0xFu);
}

float clamp_scale_from_f32(float v) {
    // 反量化乘子先限制到非负，避免把默认/脏输入直接带入 fpmul。
    return (v < 0.0f) ? 0.0f : v;
}

std::vector<uint8_t> pack_u4_words(const std::vector<uint8_t>& nibbles) {
    std::vector<uint8_t> out;
    out.reserve((nibbles.size() + 1u) / 2u);
    for (size_t i = 0; i < nibbles.size(); i += 2u) {
        const uint8_t lo = static_cast<uint8_t>(nibbles[i] & 0xFu);
        const uint8_t hi = static_cast<uint8_t>((i + 1u < nibbles.size()) ? (nibbles[i + 1u] & 0xFu) : 0u);
        out.push_back(static_cast<uint8_t>(lo | static_cast<uint8_t>(hi << 4)));
    }
    return out;
}

// B_q 分片：每个 SMC 对应 32x4，共 128 个 int4，压缩后 64B=4 个 128b word。
// 列映射采用用户指定对应关系（1-based: smc_id+1, +9, +17, +25）：
//   0-based: col = s + 8*n, n=0..3
// 每列按 G0/G1/G2/G3 组织（每组 8 个 int4，共 32 个 int4 = 1 个 128b word）。
void pack_bq_u4_per_smc_kblock_impl(const std::vector<uint8_t>& bq_u4,
                                    uint32_t num_smc,
                                    uint32_t k_block,
                                    std::vector<std::vector<uint8_t>>& smc_banks);

void pack_bq_u4_per_smc(const std::vector<uint8_t>& bq_u4,
                        uint32_t num_smc,
                        std::vector<std::vector<uint8_t>>& smc_banks) {
    pack_bq_u4_per_smc_kblock_impl(bq_u4, num_smc, 0u, smc_banks);
}

void pack_bq_u4_per_smc_kblock_impl(const std::vector<uint8_t>& bq_u4,
                               uint32_t num_smc,
                               uint32_t k_block,
                               std::vector<std::vector<uint8_t>>& smc_banks) {
    smc_banks.clear();
    constexpr size_t kGlobalDim = 64u;
    constexpr uint32_t kColsPerSmc = 4u;
    constexpr uint32_t kKChunk = 32u;
    static const uint32_t kGroups[4][8] = {
        {1, 3, 5, 7, 9, 11, 13, 15},      // G0: 2,4,...,16 (0-based row)
        {0, 2, 4, 6, 8, 10, 12, 14},      // G1: 1,3,...,15
        {17, 19, 21, 23, 25, 27, 29, 31}, // G2: 18,20,...,32
        {16, 18, 20, 22, 24, 26, 28, 30}  // G3: 17,19,...,31
    };
    if (bq_u4.size() < kGlobalDim * kGlobalDim || num_smc == 0) return;
    smc_banks.resize(num_smc);
    for (uint32_t s = 0; s < num_smc; ++s) {
        std::vector<uint8_t> qvals;
        qvals.reserve(128u);
        for (uint32_t n = 0u; n < kColsPerSmc; ++n) {
            const uint32_t col = s + 8u * n;  // smc_id + 8*n
            for (uint32_t g = 0u; g < 4u; ++g) {
                for (uint32_t i = 0u; i < 8u; ++i) {
                    const uint32_t row = kGroups[g][i] + k_block * kKChunk;
                    const uint8_t q = (col < 32u && row < kGlobalDim)
                                          ? bq_u4[static_cast<size_t>(row) * kGlobalDim + col]
                                          : 0u;
                    qvals.push_back(static_cast<uint8_t>(q & 0xFu));
                }
            }
        }
        smc_banks[s] = pack_u4_words(qvals);
    }
}

// 将 8x8 transpose+interleave2 的整包 A（8 words）切到各 SMC 的 UR bank。
// broadcast shiftdn：port1 读 addr0、port2 读 addr1（交错）；各 SMC 取 word[smc_id] / word[smc_id+8]。
// addr=2.. 对应后续 K 块，按列对子组 (pair,half) 取 full_pack 余下 words。
void pack_a_per_smc_kblock_impl(const std::vector<float>& a,
                                uint32_t num_smc,
                                uint32_t words_per_smc,
                                uint32_t k_block,
                                std::vector<std::vector<uint8_t>>& smc_banks);

void pack_a_per_smc_transpose_interleave2(const std::vector<float>& a,
                                          uint32_t num_smc,
                                          uint32_t words_per_smc,
                                          std::vector<std::vector<uint8_t>>& smc_banks) {
    pack_a_per_smc_kblock_impl(a, num_smc, words_per_smc, 0u, smc_banks);
}

void pack_a_per_smc_kblock_impl(const std::vector<float>& a,
                           uint32_t num_smc,
                           uint32_t words_per_smc,
                           uint32_t k_block,
                           std::vector<std::vector<uint8_t>>& smc_banks) {
    smc_banks.clear();
    constexpr size_t kDim = 64u;
    if (a.size() < kDim * kDim || num_smc == 0 || words_per_smc == 0) return;

    std::vector<uint8_t> full;
    pack_a_transpose_interleave2_kchunk(a, full, k_block);
    const size_t total_words = full.size() / 16u;
    if (total_words == 0) return;

    smc_banks.resize(num_smc);
    for (uint32_t s = 0; s < num_smc; ++s) {
        std::vector<uint8_t> bytes;
        bytes.reserve(static_cast<size_t>(words_per_smc) * 16u);
        for (uint32_t w = 0; w < words_per_smc; ++w) {
            const size_t word_idx =
                (static_cast<size_t>(s) + static_cast<size_t>(w) * static_cast<size_t>(num_smc)) %
                total_words;
            const size_t off = word_idx * 16u;
            bytes.insert(bytes.end(), full.begin() + static_cast<ptrdiff_t>(off),
                         full.begin() + static_cast<ptrdiff_t>(off + 16u));
        }
        smc_banks[s] = std::move(bytes);
    }
}

// ==== 新布局：A/B 逻辑转置 + 行主序密铺 + 2-word 块轮流分发（供 generate_hbm_data 写 DDR）====

// A→Aᵀ(32×8) 行主序密铺：对 K 块 [k_base, k_base+32)，for k: for row(0..7) 写 fp16(A[row][k])。
// 每 word = 一个 k 的 8 行 = 8 fp16；共 32 word。
void pack_a_transpose_dense_kchunk(const std::vector<float>& a,
                                   std::vector<uint8_t>& full, uint32_t k_block) {
    full.clear();
    constexpr size_t kARows = 8u;
    constexpr uint32_t kKChunk = 32u;
    // A 为 8×K（row-major，stride=K）。K 由张量尺寸推得，支持任意 32 倍数的 K。
    if (a.empty() || (a.size() % kARows) != 0) return;
    const size_t kTot = a.size() / kARows;
    const size_t k_base = static_cast<size_t>(k_block) * kKChunk;
    if (k_base + kKChunk > kTot) return;
    for (uint32_t kk = 0; kk < kKChunk; ++kk) {
        const size_t k = k_base + kk;
        for (size_t row = 0; row < kARows; ++row) {
            append_fp16_le(full, a[row * kTot + k]);
        }
    }
}

// B 列优先密铺 u4：for col(0..31): for k(0..31) 写 u4(bq[k][col])。
// 每 word = 原矩阵「一列」的 32 个 u4 = 16B；共 32 word（word j = 原 B 的第 j 列）。
// n_stride/col_base：N-tile 列窗（见 pack_bq_dense_interleave_kblock）。
// 本 case 只打包全局列 [col_base, col_base+32)，其在 B 张量里的行 stride = n_stride。
void pack_b_transpose_dense_kchunk(const std::vector<uint8_t>& bq_u4,
                                   std::vector<uint8_t>& full, uint32_t k_block,
                                   uint32_t n_stride = 64u, uint32_t col_base = 0u) {
    full.clear();
    const size_t kNCols = static_cast<size_t>(n_stride);  // B 列数（N，文件宽度）
    constexpr size_t kTileCols = 32u;  // 本 case 打包的 32 列（8 SMC × 4 列）
    constexpr uint32_t kKChunk = 32u;
    // B 为 K×N（row-major）。K 由张量尺寸推得，支持任意 32 倍数的 K。
    if (bq_u4.empty() || kNCols == 0u || (bq_u4.size() % kNCols) != 0) return;
    if (static_cast<size_t>(col_base) + kTileCols > kNCols) return;
    const size_t kTot = bq_u4.size() / kNCols;
    const size_t k_base = static_cast<size_t>(k_block) * kKChunk;
    if (k_base + kKChunk > kTot) return;
    std::vector<uint8_t> nibbles;
    nibbles.reserve(static_cast<size_t>(kKChunk) * kTileCols);
    for (size_t col = 0; col < kTileCols; ++col) {
        for (uint32_t kk = 0; kk < kKChunk; ++kk) {
            // smi_ds_t 仍用 LINE_NUM-1-gi_idx：转置后 A′ 一个 128b 的 lane0=组内 K7…lane7=K0。
            // B 组内必须同样倒序，fp16mul 才能 A[k]×B[k]。曾按 little-endian 吐数改成正序，
            // 但 ds_t 已恢复大端；CALC 上把 A 的 8 lane 反序后与 unscaled golden 对齐。
            const uint32_t k_group = kk / 8u;
            const uint32_t k_in_group = kk % 8u;
            const size_t k = k_base + static_cast<size_t>(k_group) * 8u + (7u - k_in_group);
            nibbles.push_back(static_cast<uint8_t>(bq_u4[k * kNCols + col] & 0xFu));
        }
    }
    full = pack_u4_words(nibbles);
}

// 2-word 块轮流分发：每 2 个连续 word 为一块，轮流发给 smc0,1,…。
// smc s 拿 word {2s, 2s+1, 2s+16, 2s+17, ...}，即 word_idx(s,w)=2*(s+num_smc*(w/2))+(w%2)。
void distribute_2word_interleave(const std::vector<uint8_t>& full, uint32_t num_smc,
                                 uint32_t words_per_smc,
                                 std::vector<std::vector<uint8_t>>& smc_banks) {
    smc_banks.clear();
    const size_t total_words = full.size() / 16u;
    if (total_words == 0 || num_smc == 0 || words_per_smc == 0) return;
    smc_banks.resize(num_smc);
    for (uint32_t s = 0; s < num_smc; ++s) {
        std::vector<uint8_t> bytes;
        bytes.reserve(static_cast<size_t>(words_per_smc) * 16u);
        for (uint32_t w = 0; w < words_per_smc; ++w) {
            const size_t word_idx =
                (2u * (static_cast<size_t>(s) + static_cast<size_t>(num_smc) * (w / 2u)) +
                 (w % 2u)) % total_words;
            const size_t off = word_idx * 16u;
            bytes.insert(bytes.end(), full.begin() + static_cast<ptrdiff_t>(off),
                         full.begin() + static_cast<ptrdiff_t>(off + 16u));
        }
        smc_banks[s] = std::move(bytes);
    }
}

// 连续列归属置换：分发【前】把 full 的 word 按固定表重排，使 2-word 间插后
// SMC s 拿到连续 4 列 {4s,4s+1,4s+2,4s+3}（旧映射为 {2s,2s+1,2s+16,2s+17}，
// 输出在列维间插乱序，不能直接当下层 dense 输入）。
// full 按 32-word 块组织（一块 = 一个 N-tile 的 32 列，word j = 列 j），置换：
//   新 word j = 旧 word perm[j]，
//   perm[j] = 4*(j/2)+(j%2)        (j<16，  对子 {4s,4s+1} 放前 16 word)
//           = 4*((j-16)/2)+(j%2)+2 (j>=16，对子 {4s+2,4s+3} 放后 16 word)
// 纯 tvgen 数据摆放选择：硬件只认 bank 内本地槽位，不感知逻辑列号；
// golden/zp/scale 用同一新映射（col=4s+n）保持一致。
void permute_words_contiguous_smc_cols(std::vector<uint8_t>& full) {
    constexpr uint32_t kBlockWords = 32u;
    const size_t total_words = full.size() / 16u;
    if (total_words == 0 || (total_words % kBlockWords) != 0) return;
    const std::vector<uint8_t> src = full;
    for (size_t blk = 0; blk < total_words / kBlockWords; ++blk) {
        for (uint32_t j = 0; j < kBlockWords; ++j) {
            const uint32_t p = (j < 16u) ? (4u * (j / 2u) + (j % 2u))
                                         : (4u * ((j - 16u) / 2u) + (j % 2u) + 2u);
            const size_t dst = (blk * kBlockWords + j) * 16u;
            const size_t frm = (blk * kBlockWords + p) * 16u;
            std::copy_n(src.begin() + static_cast<ptrdiff_t>(frm), 16u,
                        full.begin() + static_cast<ptrdiff_t>(dst));
        }
    }
}

// B 侧按“列优先 + K四组(G0/G1/G2/G3)”组织：
// 对每一列 j，连续输出 4 个 128b word：
//   G0={2,4,...,16}, G1={1,3,...,15}, G2={18,20,...,32}, G3={17,19,...,31}
// 每个 word 为 8 个 fp16（按组内升序），用于与 A 侧四拍分组对齐。
void pack_b_transpose_interleave2_8x8(const std::vector<float>& b, std::vector<uint8_t>& out) {
    out.clear();
    // 仅打包首个计算块：B_tile = B[0:32, 0:32]，按列分组写入。
    constexpr size_t kGlobalDim = 64u;
    constexpr size_t kTileRows = 32u;
    constexpr size_t kTileCols = 32u;
    if (b.size() < kGlobalDim * kGlobalDim) return;
    static const size_t kGroups[4][8] = {
        {1, 3, 5, 7, 9, 11, 13, 15},      // G0: 2,4,...,16 (0-based)
        {0, 2, 4, 6, 8, 10, 12, 14},      // G1: 1,3,...,15
        {17, 19, 21, 23, 25, 27, 29, 31}, // G2: 18,20,...,32
        {16, 18, 20, 22, 24, 26, 28, 30}  // G3: 17,19,...,31
    };
    for (size_t col = 0; col < kTileCols; ++col) {
        for (size_t g = 0; g < 4u; ++g) {
            for (size_t i = 0; i < 8u; ++i) {
                // 每个 128b word 内按“第一个数在最前面(高位)”输出。
                const size_t src_row = kGroups[g][7u - i];
                append_fp16_le(out, b[src_row * kGlobalDim + col]);
            }
        }
    }
}

// B 侧分片存放（按列连续切分 + 竖向落 bank1）：
// - 32x32 的 B_tile 按列分给 8 个 SMC，每个 SMC 连续 4 列：
//   smc0: col[0..3], smc1: col[4..7], ..., smc7: col[28..31]
// - 对单个 SMC 的 32x4 子矩阵，按行优先线性化：
//   seq = [B(0,c0),B(0,c1),B(0,c2),B(0,c3), B(1,c0),...,B(31,c3)]，共 128 个 fp16
// - bank1 写入采用“竖向”排列（addr 方向优先）：
//   idx -> word = idx % 16, lane = idx / 16
//   即先填满 lane0 的 addr0..15，再填 lane1 的 addr0..15，依次到 lane7。
// - 这样仍是每 SMC 16 个 128b word（每 word 8 个 fp16）。
[[maybe_unused]] void pack_b_per_smc_transpose_interleave2(const std::vector<float>& b,
                                                            uint32_t num_smc,
                                                            uint32_t words_per_smc,
                                                            std::vector<std::vector<uint8_t>>& smc_banks) {
    smc_banks.clear();
    constexpr size_t kGlobalDim = 64u;
    constexpr uint32_t kTileCols = 32u;
    constexpr uint32_t kTileRows = 32u;
    constexpr uint32_t kColsPerSmc = 4u;  // 32 列 / 8 SMC
    if (b.size() < kGlobalDim * kGlobalDim || num_smc == 0 || words_per_smc == 0) return;
    smc_banks.resize(num_smc);
    for (uint32_t s = 0; s < num_smc; ++s) {
        std::vector<std::array<float, 8>> words(static_cast<size_t>(words_per_smc),
                                                std::array<float, 8>{0, 0, 0, 0, 0, 0, 0, 0});
        const uint32_t col_base = s * kColsPerSmc;
        for (uint32_t row = 0u; row < kTileRows; ++row) {
            for (uint32_t dc = 0u; dc < kColsPerSmc; ++dc) {
                const uint32_t col = col_base + dc;
                const uint32_t idx = row * kColsPerSmc + dc;  // 0..127
                const uint32_t word = (words_per_smc == 0) ? 0u : (idx % words_per_smc);
                const uint32_t lane = (words_per_smc == 0) ? 0u : (idx / words_per_smc);
                if (word >= words_per_smc || lane >= 8u || col >= kTileCols) continue;
                words[static_cast<size_t>(word)][static_cast<size_t>(lane)] =
                    b[static_cast<size_t>(row) * kGlobalDim + col];
            }
        }
        std::vector<std::array<float, 8>> ordered = words;
        if (words_per_smc == 16u) {
            // 第一步：先按用户要求做两段翻转（addr[0..7] 与 addr[8..15] 各自上下+左右翻转）
            std::vector<std::array<float, 8>> flipped(16u, std::array<float, 8>{0, 0, 0, 0, 0, 0, 0, 0});
            for (uint32_t w = 0u; w < 16u; ++w) {
                uint32_t src_w = w;
                if (w < 8u) {
                    src_w = 7u - w;
                } else {
                    src_w = 23u - w;  // 8..15 -> 15..8
                }
                for (uint32_t lane = 0u; lane < 8u; ++lane) {
                    const uint32_t src_lane = 7u - lane;
                    flipped[w][lane] = words[src_w][src_lane];
                }
            }

            // 第二步：重排为与 bank3 一致的“外观顺序”：
            // rows[0..7] 取 lane[0..3]，每个 lane 由上/下半块交替拼接；
            // rows[8..15] 取 lane[4..7]，同样按上/下半块交替拼接。
            for (uint32_t r = 0u; r < 8u; ++r) {
                ordered[r][0] = flipped[r][0];
                ordered[r][1] = flipped[r + 8u][0];
                ordered[r][2] = flipped[r][1];
                ordered[r][3] = flipped[r + 8u][1];
                ordered[r][4] = flipped[r][2];
                ordered[r][5] = flipped[r + 8u][2];
                ordered[r][6] = flipped[r][3];
                ordered[r][7] = flipped[r + 8u][3];
            }
            for (uint32_t r = 8u; r < 16u; ++r) {
                const uint32_t rr = r - 8u;
                ordered[r][0] = flipped[rr][4];
                ordered[r][1] = flipped[rr + 8u][4];
                ordered[r][2] = flipped[rr][5];
                ordered[r][3] = flipped[rr + 8u][5];
                ordered[r][4] = flipped[rr][6];
                ordered[r][5] = flipped[rr + 8u][6];
                ordered[r][6] = flipped[rr][7];
                ordered[r][7] = flipped[rr + 8u][7];
            }
        }

        std::vector<std::array<float, 8>> final_words = ordered;
        if (words_per_smc == 16u) {
            // 与 bank3 目标顺序对齐：
            // - 上下半块互换（row + 8 mod 16）
            // - 每两个 lane 做一次左右交换：(0<->1),(2<->3),(4<->5),(6<->7)
            for (uint32_t r = 0u; r < 16u; ++r) {
                const auto& src = ordered[(r + 8u) % 16u];
                final_words[r][0] = src[1];
                final_words[r][1] = src[0];
                final_words[r][2] = src[3];
                final_words[r][3] = src[2];
                final_words[r][4] = src[5];
                final_words[r][5] = src[4];
                final_words[r][6] = src[7];
                final_words[r][7] = src[6];
            }
        }

        std::vector<uint8_t> bytes;
        bytes.reserve(static_cast<size_t>(words_per_smc) * 16u);
        for (uint32_t w = 0u; w < words_per_smc; ++w) {
            for (uint32_t lane = 0u; lane < 8u; ++lane) {
                append_fp16_le(bytes, final_words[static_cast<size_t>(w)][static_cast<size_t>(lane)]);
            }
        }
        // 若 words_per_smc 超过有效词数，用 0 填充到固定长度。
        while (bytes.size() < static_cast<size_t>(words_per_smc) * 16u) {
            bytes.insert(bytes.end(), 16u, 0u);
        }
        smc_banks[s] = std::move(bytes);
    }
}

// C(8x8, fp32) 的“转置 + 间插=2”打包：
// 每个 128b word 含 4 个 fp32，按 [r0,c], [r1,c], [r0,c+1], [r1,c+1] 组织。
void pack_c_transpose_interleave2_8x8(const std::vector<float>& c, std::vector<uint8_t>& out) {
    out.clear();
    constexpr size_t kDim = 64u;
    if (c.size() < kDim * kDim) return;
    for (size_t col_pair = 0; col_pair < kDim; col_pair += 2u) {
        for (size_t row_blk = 0; row_blk < (kDim / 2u); ++row_blk) {
            const size_t r0 = row_blk * 2u;
            const size_t r1 = r0 + 1u;
            append_fp32_le(out, c[r0 * kDim + col_pair]);
            append_fp32_le(out, c[r1 * kDim + col_pair]);
            append_fp32_le(out, c[r0 * kDim + (col_pair + 1u)]);
            append_fp32_le(out, c[r1 * kDim + (col_pair + 1u)]);
        }
    }
}

uint64_t serialize_header_bits(uint8_t vld, uint8_t jump, uint8_t embed,
                               uint16_t loops, uint8_t cond, uint8_t dly,
                               uint8_t inc, uint8_t embed_end_bits,
                               uint8_t inc_embed_bits) {
    constexpr uint32_t BITS_EMBED = 3, BITS_EMBED_END = 6, BITS_LOOPS = 10;
    constexpr uint32_t BITS_COND = 7, BITS_DLY = 3, BITS_INC = 4, BITS_INC_EMBED = 6;
    constexpr uint32_t MASK_EMBED = (1u << BITS_EMBED) - 1;
    constexpr uint32_t MASK_EMBED_END = (1u << BITS_EMBED_END) - 1;
    constexpr uint32_t MASK_LOOPS = (1u << BITS_LOOPS) - 1;
    constexpr uint32_t MASK_COND = (1u << BITS_COND) - 1;
    constexpr uint32_t MASK_DLY = (1u << BITS_DLY) - 1;
    constexpr uint32_t MASK_INC = (1u << BITS_INC) - 1;
    constexpr uint32_t MASK_INC_EMBED = (1u << BITS_INC_EMBED) - 1;
    uint64_t bits = 0;
    uint32_t pos = 0;
    const uint8_t ee = (embed == 0) ? static_cast<uint8_t>(embed_end_bits & MASK_EMBED_END) : 0u;
    const uint8_t ie = (embed == 0) ? static_cast<uint8_t>(inc_embed_bits & MASK_INC_EMBED) : 0u;
    bits |= static_cast<uint64_t>(ie & MASK_INC_EMBED) << pos; pos += BITS_INC_EMBED;
    bits |= static_cast<uint64_t>(inc & MASK_INC) << pos; pos += BITS_INC;
    bits |= static_cast<uint64_t>(dly & MASK_DLY) << pos; pos += BITS_DLY;
    bits |= static_cast<uint64_t>(cond & MASK_COND) << pos; pos += BITS_COND;
    bits |= static_cast<uint64_t>(loops & MASK_LOOPS) << pos; pos += BITS_LOOPS;
    bits |= static_cast<uint64_t>(ee & MASK_EMBED_END) << pos; pos += BITS_EMBED_END;
    bits |= static_cast<uint64_t>(embed & MASK_EMBED) << pos; pos += BITS_EMBED;
    bits |= static_cast<uint64_t>(jump & 1u) << pos; pos += 1;
    bits |= static_cast<uint64_t>(vld & 1u) << pos;
    return bits;
}

std::vector<uint8_t> serialize_code_msb(uint64_t code, uint16_t code_bits) {
    std::vector<uint8_t> bytes;
    const uint16_t num_bytes = static_cast<uint16_t>((code_bits + 7u) / 8u);
    for (int i = static_cast<int>(num_bytes) - 1; i >= 0; --i) {
        bytes.push_back(static_cast<uint8_t>((code >> (i * 8)) & 0xFFu));
    }
    return bytes;
}

std::vector<uint8_t> serialize_code_from_words(const uint32_t* words, int nwords, int code_bits) {
    std::vector<uint8_t> bytes(static_cast<size_t>((code_bits + 7) / 8), 0);
    for (int b = 0; b < code_bits; ++b) {
        const int wi = b / 32;
        const int bi = b % 32;
        if (wi >= nwords) break;
        if ((words[wi] >> bi) & 1u) {
            const int msb = code_bits - 1 - b;
            bytes[static_cast<size_t>(msb / 8)] |= static_cast<uint8_t>(1u << (7 - (msb % 8)));
        }
    }
    return bytes;
}

std::vector<uint8_t> make_instruction(uint8_t vld, uint8_t jump, uint8_t embed,
                                      uint16_t loops, uint8_t cond, uint8_t dly,
                                      int8_t inc, uint8_t ee, uint8_t ie,
                                      const std::vector<uint8_t>& code_bytes,
                                      int code_bits) {
    // 不能再用 uint64_t 拼整条指令：当 (41 + code_bits) > 64 会发生高位截断，
    // 例如 IQ93(smi_ds_t, code_bits=5) 仍按旧逻辑裁剪/扩展，会导致 vld 不生效。
    const uint64_t h = serialize_header_bits(vld, jump, embed, loops, cond, dly,
                                             static_cast<uint8_t>(inc & 0xF), ee, ie);
    const int tw = 41 + code_bits;
    const int nb = (tw + 7) / 8;
    std::vector<uint8_t> out(static_cast<size_t>(nb), 0);

    auto set_bit = [&](int bit_pos, int bit_v) {
        if (!bit_v) return;
        out[static_cast<size_t>(nb - 1 - bit_pos / 8)] |=
            static_cast<uint8_t>(1u << (bit_pos % 8));
    };

    // 低位先放 code（LSB 对齐）
    const int code_nb = static_cast<int>(code_bytes.size());
    const int code_pad = code_nb * 8 - code_bits;  // code_bytes 最高字节中的无效前导位
    for (int b = 0; b < code_bits; ++b) {
        const int from_msb = code_bits - 1 - b;
        const int stream_idx = code_pad + from_msb;
        const int byte_idx = stream_idx / 8;
        const int bit_in_byte = 7 - (stream_idx % 8);
        int v = 0;
        if (byte_idx >= 0 && byte_idx < code_nb) {
            v = (code_bytes[static_cast<size_t>(byte_idx)] >> bit_in_byte) & 1u;
        }
        set_bit(b, v);
    }

    // 高位放 41-bit header
    for (int b = 0; b < 41; ++b) {
        set_bit(code_bits + b, static_cast<int>((h >> b) & 1u));
    }
    return out;
}

std::vector<uint8_t> serialize_ur_w_inst_33(uint8_t vld, uint8_t jump, uint8_t embed,
                                            uint16_t loops, uint8_t cond, uint8_t dly,
                                            uint8_t inc, uint8_t ee, uint8_t ie,
                                            uint64_t code_33b) {
    const uint64_t header = serialize_header_bits(vld, jump, embed, loops, cond, dly, inc, ee, ie);
    constexpr int kCodeBits = 33;
    constexpr int kTotalBits = 41 + kCodeBits;
    const unsigned __int128 inst =
        (static_cast<unsigned __int128>(header) << kCodeBits) |
        (static_cast<unsigned __int128>(code_33b) &
         ((static_cast<unsigned __int128>(1) << kCodeBits) - 1));
    std::vector<uint8_t> out;
    const int nb = (kTotalBits + 7) / 8;
    for (int i = nb - 1; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>((inst >> (i * 8)) & 0xFFu));
    }
    return out;
}

std::vector<uint8_t> make_shiftup_code_bytes(const iq::ShiftUpCru& cru) {
    uint32_t w[5]{};
    iq::ShiftUpIq::pack_cru_shiftup(cru, w);
    return serialize_code_from_words(w, 5, 135);
}

std::vector<uint8_t> make_shiftup_inst(uint8_t vld, uint8_t jump, uint8_t embed,
                                       uint16_t loops, uint8_t cond, uint8_t dly,
                                       int8_t inc, uint8_t ee, uint8_t ie,
                                       const iq::ShiftUpCru& cru) {
    constexpr int kCodeBits = 135;
    constexpr int kTotalBits = 41 + kCodeBits;
    constexpr int kNb = (kTotalBits + 7) / 8;
    std::vector<uint8_t> out(kNb, 0);
    auto set_bit = [&](int bit_pos, int v) {
        if (!v) return;
        out[kNb - 1 - bit_pos / 8] |= static_cast<uint8_t>(1u << (bit_pos % 8));
    };
    set_bit(0, cru.para_vld ? 1 : 0);
    for (int i = 0; i < 16; ++i) {
        for (int bi = 0; bi < 8; ++bi) {
            set_bit(1 + i * 8 + bi, (cru.para_data[i] >> bi) & 1);
        }
    }
    set_bit(129, cru.para_broadcast ? 1 : 0);
    for (int bi = 0; bi < 5; ++bi) {
        set_bit(130 + bi, (cru.para_smc_id >> bi) & 1);
    }
    const uint64_t h = serialize_header_bits(vld, jump, embed, loops, cond, dly,
                                             static_cast<uint8_t>(inc & 0xF), ee, ie);
    for (int b = 0; b < 41; ++b) {
        set_bit(kCodeBits + b, static_cast<int>((h >> b) & 1u));
    }
    return out;
}

void append_nop_loops(std::vector<uint8_t>& out, uint16_t code_bits,
                      uint16_t loops, const std::vector<uint8_t>& zero_code) {
    if (loops == 0) return;
    auto nop = make_instruction(1, 0, 0, static_cast<uint16_t>(loops - 1), 0, 0, 0, 0, 0,
                                zero_code, static_cast<int>(code_bits));
    out.insert(out.end(), nop.begin(), nop.end());
}

void append_active(std::vector<uint8_t>& out, uint16_t code_bits,
                   const std::vector<uint8_t>& code) {
    auto inst = make_instruction(1, 0, 0, 0, 0, 0, 0, 0, 0, code, static_cast<int>(code_bits));
    out.insert(out.end(), inst.begin(), inst.end());
}

// 把 N 次"同 code、按固定间隔 stride 连发"的事件压成 1 条指令：
//   vld=1, loops=N-1, dly=(stride-1)。
//   dly>0 时 IQ 硬件每次触发之间会插入 dly 拍空闲。
// stride==1 → 连续 N 拍 vld=1（用于 cru_s0123 那种"每拍都发"）。
// loops 字段 10 bits：N ≤ 1024。dly 字段 3 bits：stride ≤ 8。
void append_active_repeat(std::vector<uint8_t>& out, uint16_t code_bits,
                          const std::vector<uint8_t>& code,
                          uint16_t repeats, uint16_t stride) {
    if (repeats == 0u) return;
    if (repeats == 1u && stride <= 1u) { append_active(out, code_bits, code); return; }
    const uint16_t loops = static_cast<uint16_t>(repeats - 1u);
    const uint8_t dly = (stride == 0u) ? 0u : static_cast<uint8_t>(stride - 1u);
    auto inst = make_instruction(1, 0, 0, loops, 0, dly, 0, 0, 0, code, static_cast<int>(code_bits));
    out.insert(out.end(), inst.begin(), inst.end());
}

// UR 读/写：同 code 连发 N 次，且地址按 base + i*inc 递增（IIC：addr = code[11:4] + i*inc）。
// inc 为 4bit 有符号；常用正步进 1/4。stride 同 append_active_repeat（拍间隔）。
void append_active_repeat_inc(std::vector<uint8_t>& out, uint16_t code_bits,
                              const std::vector<uint8_t>& code, uint16_t repeats,
                              uint16_t stride, int8_t inc) {
    if (repeats == 0u) return;
    if (repeats == 1u && stride <= 1u && inc == 0) {
        append_active(out, code_bits, code);
        return;
    }
    const uint16_t loops = static_cast<uint16_t>(repeats - 1u);
    const uint8_t dly = (stride == 0u) ? 0u : static_cast<uint8_t>(stride - 1u);
    auto inst =
        make_instruction(1, 0, 0, loops, 0, dly, inc, 0, 0, code, static_cast<int>(code_bits));
    out.insert(out.end(), inst.begin(), inst.end());
}

// 单条压缩 IQ：从 start_cycle 起，用 loops+dly 发 repeats 次 (间隔 stride 拍) 同 code 触发。
// 之后至 total_cycles 全部 idle。
void build_iq_timeline_single_burst(uint32_t iq_id, const std::string& iq_name, uint16_t code_bits,
                                    uint16_t start_cycle, uint16_t repeats, uint16_t stride,
                                    const std::vector<uint8_t>& code,
                                    uint16_t total_cycles, std::vector<IqInstructionFile>& files) {
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = static_cast<uint32_t>(41 + code_bits);
    const auto zero_code = serialize_code_msb(0, code_bits);
    if (start_cycle > 0u) append_nop_loops(f.para_data, code_bits, start_cycle, zero_code);
    append_active_repeat(f.para_data, code_bits, code, repeats, stride == 0u ? 1u : stride);
    const uint32_t burst_span = static_cast<uint32_t>(repeats) *
                                static_cast<uint32_t>(stride == 0u ? 1u : stride);
    const uint32_t after_burst = static_cast<uint32_t>(start_cycle) + burst_span;
    if (total_cycles > after_burst) {
        append_nop_loops(f.para_data, code_bits,
                         static_cast<uint16_t>(total_cycles - after_burst), zero_code);
    }
    auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

// 单条压缩 IQ + 地址自增：同 build_iq_timeline_single_burst，但 IIC 逐次对 code[11:4]
// 地址字段 +inc（仅 UR 读/写这类地址落在 code[11:4] 的指令有效，smi_iq_iic.v w_operate_micro）。
void build_iq_timeline_single_burst_inc(uint32_t iq_id, const std::string& iq_name,
                                        uint16_t code_bits, uint16_t start_cycle,
                                        uint16_t repeats, uint16_t stride, int8_t inc,
                                        const std::vector<uint8_t>& code, uint16_t total_cycles,
                                        std::vector<IqInstructionFile>& files) {
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = static_cast<uint32_t>(41 + code_bits);
    const auto zero_code = serialize_code_msb(0, code_bits);
    if (start_cycle > 0u) append_nop_loops(f.para_data, code_bits, start_cycle, zero_code);
    append_active_repeat_inc(f.para_data, code_bits, code, repeats, stride == 0u ? 1u : stride,
                             inc);
    const uint32_t st = (stride == 0u) ? 1u : stride;
    const uint32_t after_burst = static_cast<uint32_t>(start_cycle) +
                                 static_cast<uint32_t>(repeats) * st;
    if (total_cycles > after_burst) {
        append_nop_loops(f.para_data, code_bits,
                         static_cast<uint16_t>(total_cycles - after_burst), zero_code);
    }
    auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

// 多段同 code 连发（vld=1），段间 NOP。同一 IQ_ID 只能写一份文件。
void build_iq_timeline_multi_burst(
    uint32_t iq_id, const std::string& iq_name, uint16_t code_bits,
    const std::vector<uint16_t>& starts, uint16_t repeats, uint16_t stride,
    const std::vector<uint8_t>& code, uint16_t total_cycles,
    std::vector<IqInstructionFile>& files) {
    if (starts.empty() || repeats == 0u) return;
    if (starts.size() == 1u) {
        build_iq_timeline_single_burst(iq_id, iq_name, code_bits, starts[0], repeats, stride,
                                       code, total_cycles, files);
        return;
    }
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = static_cast<uint32_t>(41 + code_bits);
    const auto zero_code = serialize_code_msb(0, code_bits);
    const uint16_t st = (stride == 0u) ? 1u : stride;
    uint16_t cur = 0;
    for (uint16_t start : starts) {
        if (start > cur) {
            append_nop_loops(f.para_data, code_bits, static_cast<uint16_t>(start - cur),
                             zero_code);
            cur = start;
        }
        append_active_repeat(f.para_data, code_bits, code, repeats, st);
        cur = static_cast<uint16_t>(cur + static_cast<uint32_t>(repeats) * st);
    }
    if (total_cycles > cur) {
        append_nop_loops(f.para_data, code_bits, static_cast<uint16_t>(total_cycles - cur),
                         zero_code);
    }
    auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

// VIN/VOUT 的 CRU 在 iq_inst_vld=0 时会被 smi_top 清零（不像 RtoVR 锁存）。
// 必须在窗口内保持 vld=1 + smc_id_en，与 pow 的 loops 常开一致。
void build_iq_valid_hold_window(uint32_t iq_id, const std::string& iq_name, uint16_t code_bits,
                                const std::vector<uint8_t>& hold_code, uint16_t t_start,
                                uint16_t t_end, uint16_t total_cycles,
                                std::vector<IqInstructionFile>& files) {
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = static_cast<uint32_t>(41 + code_bits);
    const auto zero_code = serialize_code_msb(0, code_bits);
    if (t_end > total_cycles) t_end = total_cycles;
    if (t_start >= t_end) {
        if (total_cycles > 0) append_nop_loops(f.para_data, code_bits, total_cycles, zero_code);
        auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
        f.para_data.insert(f.para_data.end(), end.begin(), end.end());
        files.push_back(std::move(f));
        return;
    }
    if (t_start > 0) append_nop_loops(f.para_data, code_bits, t_start, zero_code);
    append_nop_loops(f.para_data, code_bits, static_cast<uint16_t>(t_end - t_start), hold_code);
    if (total_cycles > t_end) {
        append_nop_loops(f.para_data, code_bits, static_cast<uint16_t>(total_cycles - t_end),
                         zero_code);
    }
    auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

void build_iq_timeline(uint32_t iq_id, const std::string& iq_name, uint16_t code_bits,
                       const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& events,
                       uint16_t total_cycles, std::vector<IqInstructionFile>& files) {
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = static_cast<uint32_t>(41 + code_bits);
    const auto zero_code = serialize_code_msb(0, code_bits);
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> sorted = events;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    uint16_t cur = 0;
    for (const auto& ev : sorted) {
        if (ev.first > cur) {
            append_nop_loops(f.para_data, code_bits, static_cast<uint16_t>(ev.first - cur), zero_code);
            cur = ev.first;
        }
        append_active(f.para_data, code_bits, ev.second);
        ++cur;
    }
    if (total_cycles > cur) {
        append_nop_loops(f.para_data, code_bits, static_cast<uint16_t>(total_cycles - cur), zero_code);
    }
    auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(f);
}

// 与 build_iq_timeline 相同，但空闲拍用 vld=0 的 NOP，避免路由口在空闲拍被反复覆盖。
void build_iq_timeline_idle_invalid(uint32_t iq_id, const std::string& iq_name, uint16_t code_bits,
                                    const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& events,
                                    uint16_t total_cycles, std::vector<IqInstructionFile>& files) {
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = static_cast<uint32_t>(41 + code_bits);
    const auto zero_code = serialize_code_msb(0, code_bits);
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> sorted = events;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    auto append_idle_loops = [&](uint16_t loops) {
        if (loops == 0) return;
        auto idle = make_instruction(0, 0, 0, static_cast<uint16_t>(loops - 1), 0, 0, 0, 0, 0,
                                     zero_code, static_cast<int>(code_bits));
        f.para_data.insert(f.para_data.end(), idle.begin(), idle.end());
    };

    uint16_t cur = 0;
    for (const auto& ev : sorted) {
        if (ev.first > cur) {
            append_idle_loops(static_cast<uint16_t>(ev.first - cur));
            cur = ev.first;
        }
        append_active(f.para_data, code_bits, ev.second);
        ++cur;
    }
    if (cur < total_cycles) {
        append_idle_loops(static_cast<uint16_t>(total_cycles - cur));
    }
    auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

// 先连续 vld=1 开窗（loops 压缩），再按 idle_invalid 追加后续事件。
// dequant 的 UR→VIN 必须每拍更新；Phase B 改挂不能再另写一份同 IQ_ID 文件。
void build_iq_active_window_then_events(
    uint32_t iq_id, const std::string& iq_name, uint16_t code_bits,
    uint16_t win_start, uint16_t win_end, const std::vector<uint8_t>& win_code,
    const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& events,
    uint16_t total_cycles, std::vector<IqInstructionFile>& files) {
    if (win_end > total_cycles) win_end = total_cycles;
    if (win_start >= win_end) {
        build_iq_timeline_idle_invalid(iq_id, iq_name, code_bits, events, total_cycles, files);
        return;
    }
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = static_cast<uint32_t>(41 + code_bits);
    const auto zero_code = serialize_code_msb(0, code_bits);
    auto append_idle_loops = [&](uint16_t loops) {
        if (loops == 0) return;
        auto idle = make_instruction(0, 0, 0, static_cast<uint16_t>(loops - 1), 0, 0, 0, 0, 0,
                                     zero_code, static_cast<int>(code_bits));
        f.para_data.insert(f.para_data.end(), idle.begin(), idle.end());
    };
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> sorted = events;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    uint16_t cur = 0;
    if (win_start > 0) {
        append_idle_loops(win_start);
        cur = win_start;
    }
    append_active_repeat(f.para_data, code_bits, win_code,
                         static_cast<uint16_t>(win_end - win_start), 1u);
    cur = win_end;
    for (const auto& ev : sorted) {
        if (ev.first < win_end) continue;
        if (ev.first > cur) {
            append_idle_loops(static_cast<uint16_t>(ev.first - cur));
            cur = ev.first;
        }
        append_active(f.para_data, code_bits, ev.second);
        ++cur;
    }
    if (cur < total_cycles) {
        append_idle_loops(static_cast<uint16_t>(total_cycles - cur));
    }
    auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

// 对齐 tb_aiacc_top_shift：IQ14 预填 16 条 WRD + JUMP；EXTRA_DATA 由 ds_t.wrd_vld 门控发射。
void build_shiftup_iq14_wrd_queue(const iq::ShiftUpCru& su, uint16_t num_beats,
                                  std::vector<IqInstructionFile>& files) {
    IqInstructionFile f;
    f.para_iq_id = iq::IQ_ID_SHIFTUP;
    f.para_iq_name = iq::IQ_NAME_SHIFTUP;
    f.para_width_bits = static_cast<uint32_t>(41 + 135);
    for (uint16_t i = 0; i < num_beats; ++i) {
        (void)i;
        auto active = make_shiftup_inst(1, 0, 0, 0, 0, 0, 0, 0, 0, su);
        f.para_data.insert(f.para_data.end(), active.begin(), active.end());
    }
    auto end = make_shiftup_inst(1, 1, 0, 0, 0, 0, 0, 0, 0, iq::ShiftUpCru{});
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

void build_iq_constant_hold(uint32_t iq_id, const std::string& iq_name, uint16_t code_bits,
                            const std::vector<uint8_t>& code, uint16_t total_cycles,
                            std::vector<IqInstructionFile>& files) {
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = static_cast<uint32_t>(41 + code_bits);
    const auto zero_code = serialize_code_msb(0, code_bits);
    if (total_cycles > 0) {
        auto hold = make_instruction(1, 0, 0, static_cast<uint16_t>(total_cycles - 1), 0, 0, 0, 0, 0,
                                     code, static_cast<int>(code_bits));
        f.para_data.insert(f.para_data.end(), hold.begin(), hold.end());
    }
    auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

// RtoVR 常态 hold_code，在 patch 拍切换为 fpadd 路由（用于 IQ33 5to1 hold + wb fpadd_s0）。
constexpr uint8_t kUrWdataActiveDly = 0;

// UR 写回事件（单口）：Phase A 写 bank2 / Phase B 写 bank7 / epilogue 写 bank6。
struct UrWbEvent {
    uint16_t fire_cycle;
    uint8_t addr_word;
    uint8_t bank;
};

// 单口 UR 写回 IQ：事件按 fire_cycle 排空，事件间与尾部用 NOP loops 填充，末尾附 end(jump_last)。
// compile_core（bank2/bank7）与 f16 epilogue（bank6）共用；base_ur 只提供 wbe/smc_id_en 等模板，
// 端口 1/2 在此统一关闭（仅写 port 指定的那一路）。
void build_ur_write_file_from_events(uint32_t iq_id, const std::string& iq_name, int port,
                                     const iq::UrCru& base_ur, uint16_t total_cycles,
                                     std::vector<UrWbEvent>& events,
                                     std::vector<IqInstructionFile>& files) {
    std::sort(events.begin(), events.end(),
              [](const UrWbEvent& a, const UrWbEvent& b) { return a.fire_cycle < b.fire_cycle; });
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = 74;

    auto pad_nops = [&](uint32_t cycles, uint16_t& cur) {
        if (cycles == 0u) return;
        uint32_t remain = cycles;
        while (remain > 1024u) {
            auto nop = serialize_ur_w_inst_33(1, 0, 0, 1023, 0, 0, 0, 0, 0, 0);
            f.para_data.insert(f.para_data.end(), nop.begin(), nop.end());
            remain -= 1024u;
        }
        if (remain > 0u) {
            auto nop = serialize_ur_w_inst_33(1, 0, 0, static_cast<uint16_t>(remain - 1u), 0, 0, 0, 0,
                                              0, 0);
            f.para_data.insert(f.para_data.end(), nop.begin(), nop.end());
        }
        cur = static_cast<uint16_t>(cur + cycles);
    };

    uint16_t cur = 0;
    if (!events.empty() && events.front().fire_cycle > 0) {
        pad_nops(events.front().fire_cycle, cur);
    }

    for (size_t t = 0; t < events.size(); ++t) {
        auto ur_tmp = base_ur;
        ur_tmp.para_we[1] = false;
        ur_tmp.para_we[2] = false;
        // per-event bank：Phase A 写 bank2，Phase B 写 bank7，epilogue 写 bank6。
        ur_tmp.para_waddr[port].bank = events[t].bank;
        ur_tmp.para_waddr[port].addr_word = events[t].addr_word;
        const uint64_t ur_code = extract_ur_write_port_code33(ur_tmp, port);
        auto wr = serialize_ur_w_inst_33(1, 0, 0, 0, 0, kUrWdataActiveDly, 0, 0, 0, ur_code);
        f.para_data.insert(f.para_data.end(), wr.begin(), wr.end());
        cur = static_cast<uint16_t>(cur + 1u);

        if (t + 1u < events.size()) {
            const uint16_t next = events[t + 1u].fire_cycle;
            if (next > cur) pad_nops(static_cast<uint32_t>(next - cur), cur);
        }
    }

    if (total_cycles > cur) {
        pad_nops(static_cast<uint32_t>(total_cycles - cur), cur);
    }

    // end 指令显式置 jump_last（IIC 以 inst[41:40]==2'b11 判定），避免队列尾部重复发射
    auto end = serialize_ur_w_inst_33(1, 1, 0, 0, 0, 0, 6, 0, 0, 0);
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

void build_rtovr_hold_with_patches(uint32_t iq_id, const std::string& iq_name, uint16_t code_bits,
                                   const std::vector<uint8_t>& hold_code,
                                   const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& patches,
                                   uint16_t total_cycles, std::vector<IqInstructionFile>& files) {
    IqInstructionFile f;
    f.para_iq_id = iq_id;
    f.para_iq_name = iq_name;
    f.para_width_bits = static_cast<uint32_t>(41 + code_bits);
    const auto zero_code = serialize_code_msb(0, code_bits);
    auto sorted = patches;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    uint16_t cur = 0;
    for (const auto& patch : sorted) {
        if (patch.first > cur) {
            const uint16_t gap = static_cast<uint16_t>(patch.first - cur);
            if (gap > 0u) {
                append_nop_loops(f.para_data, code_bits, gap, hold_code);
            }
            cur = patch.first;
        }
        append_active(f.para_data, code_bits, patch.second);
        cur = static_cast<uint16_t>(patch.first + 1u);
    }
    if (cur < total_cycles) {
        append_nop_loops(f.para_data, code_bits, static_cast<uint16_t>(total_cycles - cur), hold_code);
    }
    auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero_code, static_cast<int>(code_bits));
    f.para_data.insert(f.para_data.end(), end.begin(), end.end());
    files.push_back(std::move(f));
}

}  // namespace

void MatmulNtW4f16Op::pack_a_per_smc_kblock(const std::vector<float>& a,
                                   uint32_t num_smc,
                                   uint32_t words_per_smc,
                                   uint32_t k_block,
                                   std::vector<std::vector<uint8_t>>& smc_banks) {
    pack_a_per_smc_kblock_impl(a, num_smc, words_per_smc, k_block, smc_banks);
}

void MatmulNtW4f16Op::pack_bq_u4_per_smc_kblock(const std::vector<uint8_t>& bq_u4,
                                       uint32_t num_smc,
                                       uint32_t k_block,
                                       std::vector<std::vector<uint8_t>>& smc_banks) {
    pack_bq_u4_per_smc_kblock_impl(bq_u4, num_smc, k_block, smc_banks);
}

// 新布局：A→Aᵀ 行主序密铺 + 2-word 块轮流分发（写 DDR/HBM）。
void MatmulNtW4f16Op::pack_a_dense_interleave_kblock(const std::vector<float>& a,
                                            uint32_t num_smc,
                                            uint32_t words_per_smc,
                                            uint32_t k_block,
                                            std::vector<std::vector<uint8_t>>& smc_banks) {
    std::vector<uint8_t> full;
    pack_a_transpose_dense_kchunk(a, full, k_block);
    distribute_2word_interleave(full, num_smc, words_per_smc, smc_banks);
}

// 新布局：B→Bᵀ 行主序密铺 u4 + 连续列置换 + 2-word 块轮流分发（写 DDR/HBM）。
// 置换后 SMC s 拥有连续列 {4s,4s+1,4s+2,4s+3}（见 permute_words_contiguous_smc_cols）。
void MatmulNtW4f16Op::pack_bq_dense_interleave_kblock(const std::vector<uint8_t>& bq_u4,
                                             uint32_t num_smc,
                                             uint32_t words_per_smc,
                                             uint32_t k_block,
                                             std::vector<std::vector<uint8_t>>& smc_banks,
                                             uint32_t n_stride,
                                             uint32_t col_base) {
    std::vector<uint8_t> full;
    pack_b_transpose_dense_kchunk(bq_u4, full, k_block, n_stride, col_base);
    permute_words_contiguous_smc_cols(full);
    distribute_2word_interleave(full, num_smc, words_per_smc, smc_banks);
}

namespace {

// M=256：32 个 row-group 各自的 Aᵀ(32×8) 块顺序拼接（组 g = 行 [8g,8g+8)）。
// 每块与 M=8 的 pack_a_transpose_dense_kchunk 完全同构（每 word = 一个 k 的组内 8 行 fp16）。
void pack_a_m256_transpose_dense_kchunk(const std::vector<float>& a,
                                        std::vector<uint8_t>& full, uint32_t k_block) {
    full.clear();
    constexpr size_t kARows = kMatmulNtW4f16M256;
    constexpr uint32_t kKChunk = 32u;
    constexpr uint32_t kRowsPerGroup = kMatmulNtW4f16M256RowsPerGroup;
    constexpr uint32_t kGroups = kMatmulNtW4f16M256Groups;
    if (a.empty() || (a.size() % kARows) != 0) return;
    const size_t kTot = a.size() / kARows;
    const size_t k_base = static_cast<size_t>(k_block) * kKChunk;
    if (k_base + kKChunk > kTot) return;
    full.reserve(static_cast<size_t>(kGroups) * kKChunk * 16u);
    for (uint32_t g = 0; g < kGroups; ++g) {
        for (uint32_t kk = 0; kk < kKChunk; ++kk) {
            const size_t k = k_base + kk;
            for (uint32_t r = 0; r < kRowsPerGroup; ++r) {
                const size_t row = static_cast<size_t>(g) * kRowsPerGroup + r;
                append_fp16_le(full, a[row * kTot + k]);
            }
        }
    }
}

}  // namespace

// M=256：32 组拼接（1024 word）后整体 2-word 间插。因 16（每组对数）≡0 (mod 8)，
// 组内 pair 分布与 M=8 一致：组 g 落在每 SMC 本地 word [4g,4g+4)。
void MatmulNtW4f16Op::pack_a_m256_dense_interleave_kblock(const std::vector<float>& a,
                                                 uint32_t num_smc,
                                                 uint32_t words_per_smc,
                                                 uint32_t k_block,
                                                 std::vector<std::vector<uint8_t>>& smc_banks) {
    std::vector<uint8_t> full;
    pack_a_m256_transpose_dense_kchunk(a, full, k_block);
    distribute_2word_interleave(full, num_smc, words_per_smc, smc_banks);
}

// fp16 B：按 K 组分层列优先密铺（与 GPTQ「一列一字」同列归属，但每列拆成 4 个 8×fp16 word）。
// 全图 4 层 ×32 列 = 128 word；层 g 的 word j = 列 j 的 K[g*8 .. g*8+8)。
// 连续列置换 + 2-word 间插后每 SMC 16 word：本地 [4g + slot] = 本 SMC 第 slot 列的第 g 组
// （slot 对应连续列 {4s,4s+1,4s+2,4s+3}，与 GPTQ 的 b_addr+(r%4) 同序）。
void matmul_nt_w4f16_pack_b_fp16_transpose_dense_kchunk(const std::vector<uint16_t>& b_fp16,
                                        std::vector<uint8_t>& full, uint32_t k_block,
                                        uint32_t n_stride = 64u, uint32_t col_base = 0u) {
    full.clear();
    const size_t kNCols = static_cast<size_t>(n_stride);
    constexpr size_t kTileCols = 32u;
    constexpr uint32_t kKChunk = 32u;
    constexpr uint32_t kGroups = 4u;
    constexpr uint32_t kGroupLanes = 8u;
    if (b_fp16.empty() || kNCols == 0u || (b_fp16.size() % kNCols) != 0) return;
    if (static_cast<size_t>(col_base) + kTileCols > kNCols) return;
    const size_t kTot = b_fp16.size() / kNCols;
    const size_t k_base = static_cast<size_t>(k_block) * kKChunk;
    if (k_base + kKChunk > kTot) return;
    full.reserve(static_cast<size_t>(kGroups) * kTileCols * 16u);
    for (uint32_t g = 0; g < kGroups; ++g) {
        for (size_t col = 0; col < kTileCols; ++col) {
            for (uint32_t i = 0; i < kGroupLanes; ++i) {
                // 与 GPTQ / ds_t 大端 A′ 对齐：lane0=组内 K7。
                const size_t k =
                    k_base + static_cast<size_t>(g) * kGroupLanes + (7u - i);
                const uint16_t h = b_fp16[k * kNCols + static_cast<size_t>(col_base) + col];
                full.push_back(static_cast<uint8_t>(h & 0xFFu));
                full.push_back(static_cast<uint8_t>((h >> 8) & 0xFFu));
            }
        }
    }
}

namespace {

// M=1：A 1×256 reshape 成假 8×32（A'[r,t]=a[32*r+t]，不补 0），再走与 M=8 相同的
// pack_a_transpose_dense（每 word = 同 K 的 8 行 fp16）→ 供 smi_ds_t 转置写 bank3。
void pack_a_m1_dense_ktile(const std::vector<float>& a, std::vector<uint8_t>& full,
                           uint32_t k_block, uint32_t k_tile) {
    full.clear();
    constexpr uint32_t kRows = 8u;
    constexpr uint32_t kCols = 32u;
    if (a.empty() || k_tile != kMatmulNtW4f16M1KTile) return;
    const size_t kTot = a.size();
    const size_t k_base = static_cast<size_t>(k_block) * static_cast<size_t>(k_tile);
    if (k_base + k_tile > kTot) return;
    std::vector<float> a8(static_cast<size_t>(kRows) * kCols, 0.0f);
    for (uint32_t r = 0; r < kRows; ++r) {
        for (uint32_t t = 0; t < kCols; ++t) {
            a8[static_cast<size_t>(r) * kCols + t] =
                a[k_base + static_cast<size_t>(r) * kCols + t];
        }
    }
    pack_a_transpose_dense_kchunk(a8, full, /*k_block=*/0u);
}

// M=1：B 256×32 = 8 段 ×(K32×32)。每段用与 M=8 相同的 pack_b_transpose_dense，
// 再首尾相接成 256 word；连续列置换 + 间插后，SMC s 本地 addr [4t .. 4t+3] =
// 第 t 段中本 SMC 的连续 4 列 {4s,4s+1,4s+2,4s+3}。
void pack_b_m1_dense_ktile(const std::vector<uint8_t>& bq_u4, std::vector<uint8_t>& full,
                           uint32_t k_block, uint32_t k_tile,
                           uint32_t n_stride = 64u, uint32_t col_base = 0u) {
    full.clear();
    constexpr uint32_t kSegs = 8u;
    constexpr uint32_t kKChunk = 32u;
    if (bq_u4.empty() || k_tile != kMatmulNtW4f16M1KTile) return;
    const size_t kTot = bq_u4.size() / static_cast<size_t>(n_stride);
    const size_t k_base = static_cast<size_t>(k_block) * static_cast<size_t>(k_tile);
    if (k_base + k_tile > kTot) return;
    full.reserve(static_cast<size_t>(kSegs) * 32u * 16u);
    for (uint32_t seg = 0; seg < kSegs; ++seg) {
        std::vector<uint8_t> chunk;
        // 绝对 K 块号：tile 内第 seg 段对应全局 k_block_m8 = k_block*8 + seg
        pack_b_transpose_dense_kchunk(
            bq_u4, chunk,
            static_cast<uint32_t>(k_block * (k_tile / kKChunk) + seg), n_stride, col_base);
        if (chunk.empty()) {
            full.clear();
            return;
        }
        full.insert(full.end(), chunk.begin(), chunk.end());
    }
}

}  // namespace

void MatmulNtW4f16Op::pack_a_m1_dense_interleave_kblock(const std::vector<float>& a,
                                               uint32_t num_smc,
                                               uint32_t words_per_smc,
                                               uint32_t k_block,
                                               std::vector<std::vector<uint8_t>>& smc_banks,
                                               bool replicate) {
    std::vector<uint8_t> full;
    pack_a_m1_dense_ktile(a, full, k_block, kMatmulNtW4f16M1KTile);
    if (full.empty() || num_smc == 0 || words_per_smc == 0) {
        smc_banks.clear();
        return;
    }
    if (replicate) {
        smc_banks.assign(num_smc, full);
        // 若要求固定 words_per_smc，截断/补零到该长度。
        const size_t want = static_cast<size_t>(words_per_smc) * 16u;
        for (auto& bank : smc_banks) {
            if (bank.size() > want) bank.resize(want);
            else if (bank.size() < want) bank.resize(want, 0);
        }
        return;
    }
    distribute_2word_interleave(full, num_smc, words_per_smc, smc_banks);
}

void MatmulNtW4f16Op::pack_bq_m1_dense_interleave_kblock(const std::vector<uint8_t>& bq_u4,
                                                uint32_t num_smc,
                                                uint32_t words_per_smc,
                                                uint32_t k_block,
                                                std::vector<std::vector<uint8_t>>& smc_banks,
                                                uint32_t n_stride,
                                                uint32_t col_base) {
    std::vector<uint8_t> full;
    pack_b_m1_dense_ktile(bq_u4, full, k_block, kMatmulNtW4f16M1KTile, n_stride, col_base);
    permute_words_contiguous_smc_cols(full);
    distribute_2word_interleave(full, num_smc, words_per_smc, smc_banks);
}

// zp 打包（路线A）：32 列，每列一个 128b word = zp[col](低4位) 复制成 16 个 u8 lane，
// 连续列置换后按与 B 完全相同的 2-word 间插分发（smc s 拿连续列 {4s,4s+1,4s+2,4s+3}）。
// 依据：intadd(add8) 逐 8-bit lane 计算 q-zp，一整列的 32 个 K 共用同一 zp，硬件无广播，
// 故必须把 zp 在 src0/src1 的 16 个 byte-lane 上全填同值。zp 语义 4-bit，物理占 1 字节。
void MatmulNtW4f16Op::pack_zp_dense_interleave(const std::vector<uint8_t>& zp_col,
                                      uint32_t num_smc,
                                      uint32_t words_per_smc,
                                      std::vector<std::vector<uint8_t>>& smc_banks,
                                      uint32_t col_base) {
    constexpr uint32_t kCols = 32u;
    std::vector<uint8_t> full(static_cast<size_t>(kCols) * 16u, 0u);
    for (uint32_t c = 0; c < kCols; ++c) {
        const size_t gcol = static_cast<size_t>(col_base) + c;   // 全局列号（N-tile 列窗）
        const uint8_t z = (gcol < zp_col.size()) ? static_cast<uint8_t>(zp_col[gcol] & 0xFu) : 0u;
        for (uint32_t b = 0; b < 16u; ++b) full[static_cast<size_t>(c) * 16u + b] = z;
    }
    permute_words_contiguous_smc_cols(full);
    distribute_2word_interleave(full, num_smc, words_per_smc, smc_banks);
}

// scale 打包（硬件实测对齐）：Phase B 是【一个 bank2 字里 4 列一起乘】，故必须把本 SMC 拥有的
// 4 列 {4s,4s+1,4s+2,4s+3}(=n0..n3) 的 scale 并进【同一个 128b 字】，而不是每列一个字。
// 布局：fp16(scale[n]) 放在 word0 的 fp16 lane 0/2/4/6（字节偏移 0/4/8/12），供 fptofp(src_pos=0)
//   取偶数 fp16 lane → 4×fp32(lane0..3, 字节 0/4/8/12) → fpmul 与 bank2 逐 fp32-lane 相乘。
//   bank2 compute-lane p(字节 4p) = 列 n_p（p0=col(4s),…,p3=col(4s+3)），故 scale 同序摆放。
//   所有 8 个 bank2 字共用这一个 scale 字（同 4 列），Phase B 读固定 word0。
// 每 SMC 只有 word0 有效，其余 word 置 0（words_per_smc 仍按 kScaleWordsPerSmc 分配以对齐尺寸）。
void MatmulNtW4f16Op::pack_scale_dense_interleave(const std::vector<float>& scale_col,
                                         uint32_t num_smc,
                                         uint32_t words_per_smc,
                                         std::vector<std::vector<uint8_t>>& smc_banks,
                                         uint32_t col_base) {
    smc_banks.assign(num_smc, std::vector<uint8_t>(static_cast<size_t>(words_per_smc) * 16u, 0u));
    for (uint32_t s = 0; s < num_smc; ++s) {
        for (uint32_t n = 0; n < 4u; ++n) {
            const uint32_t col = col_base + 4u * s + n;   // 全局列号（N-tile 列窗）
            const float sv = (col < scale_col.size()) ? scale_col[col] : 1.0f;
            const uint16_t h = app::float32_to_float16(sv);
            // 列 n 对应硬件 fp32 lane (3-n)（因 bank 转储整字倒序），fptofp(src_pos=0) 取 fp16 偶数
            // lane 2p→fp32 lane p，故 scale[col_n] 放 fp16 lane 2*(3-n) = 字节偏移 (3-n)*4。
            const size_t off = static_cast<size_t>(3u - n) * 4u;
            smc_banks[s][off + 0] = static_cast<uint8_t>(h & 0xFFu);
            smc_banks[s][off + 1] = static_cast<uint8_t>((h >> 8) & 0xFFu);
        }
    }
}

namespace {

constexpr uint32_t kGoldenKChunk = 32u;
constexpr uint32_t kGoldenRows = 8u;
constexpr uint32_t kGoldenColsPerSmc = 4u;

float fp16_roundtrip(float v) {
    return app::float16_to_float32(app::float32_to_float16(v));
}

void write_fp32_le(std::vector<uint8_t>& out, size_t byte_off, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    out[byte_off + 0] = static_cast<uint8_t>(bits & 0xFFu);
    out[byte_off + 1] = static_cast<uint8_t>((bits >> 8) & 0xFFu);
    out[byte_off + 2] = static_cast<uint8_t>((bits >> 16) & 0xFFu);
    out[byte_off + 3] = static_cast<uint8_t>((bits >> 24) & 0xFFu);
}

}  // namespace

bool MatmulNtW4f16Op::compute_bank2_golden_kchunk(uint8_t smc_id,
                                         const std::vector<float>& a,
                                         const std::vector<uint8_t>& bq_u4,
                                         const std::vector<uint8_t>& zp_col,
                                         const std::vector<float>& scale_col,
                                         uint32_t k_block,
                                         std::vector<uint8_t>& bank2_out,
                                         uint32_t a_rows,
                                         uint32_t row_base,
                                         uint32_t n_stride,
                                         uint32_t col_base) {
    // A：a_rows×K（stride=K）；B：K×n_stride（stride=N）；zp/scale：全局 N 宽向量，本 case 取
    // [col_base, col_base+32)。K 由尺寸推得。本函数只算行 [row_base, row_base+8)
    // （M=256 组 g → row_base=8g）。
    const size_t kNCols = static_cast<size_t>(n_stride);
    if (a.empty() || a_rows == 0u || kNCols == 0u || (a.size() % a_rows) != 0 ||
        static_cast<size_t>(row_base) + kGoldenRows > a_rows) {
        return false;
    }
    const size_t kTot = a.size() / a_rows;
    if (bq_u4.size() < kTot * kNCols) {
        return false;
    }
    if (zp_col.size() < static_cast<size_t>(col_base) + 32u ||
        scale_col.size() < static_cast<size_t>(col_base) + 32u) {
        return false;
    }
    const uint32_t k_base = k_block * kGoldenKChunk;
    if (k_base + kGoldenKChunk > kTot) {
        return false;
    }

    // 硬件实测对齐（seed 复现 + bank2 逐 lane 匹配）：
    //  - partial 未乘 scale：Phase A 只算 sum_k fp16(A)·fp16(q-zp)，落 bank2；
    //    scale 在 Phase B 统一乘一次（C = scale · Σ，scale 为该列常数，等价逐 K 乘）。
    //  - 写回是【行主序】：bank2/bank7 word w = 第 w 行，一个字里 4 个 fp32 = 该 SMC 拥有的
    //    4 个连续列 {4s,4s+1,4s+2,4s+3}（n=0..3）。字内列顺序（硬件 fp32 lane）= [n3,n2,n1,n0]；
    //    换算到 write_fp32_le 字节偏移(offset0→lane3, offset12→lane0)后，
    //    offset 0/4/8/12 恰好 = n0/n1/n2/n3（自然序）。
    std::array<std::array<float, kGoldenColsPerSmc>, kGoldenRows> c_tile{};
    std::array<std::array<float, kGoldenColsPerSmc>, kGoldenRows> partial_tile{};  // 未乘 scale
    for (uint32_t row = 0; row < kGoldenRows; ++row) {
        for (uint32_t n = 0; n < kGoldenColsPerSmc; ++n) {
            const uint32_t col = col_base + 4u * static_cast<uint32_t>(smc_id) + n;
            float acc = 0.0f;  // Phase A：未乘 scale 的 partial（fp32 累加）
            for (uint32_t k = k_base; k < k_base + kGoldenKChunk; ++k) {
                const float af =
                    fp16_roundtrip(a[static_cast<size_t>(row_base + row) * kTot + static_cast<size_t>(k)]);
                const int32_t diff =
                    static_cast<int32_t>(bq_u4[static_cast<size_t>(k) * kNCols +
                                               static_cast<size_t>(col)] & 0xFu) -
                    static_cast<int32_t>(zp_col[col] & 0xFu);
                const float diff_f = fp16_roundtrip(static_cast<float>(diff));
                // 硬件 fp16mul：fp16×fp16→fp32，乘积【不】再舍回 fp16（实测 bit-exact 验证），
                // 随后 fp32 加法树累加。故这里用 fp32 乘积直接累加，勿用 fp16_mul_fp32。
                acc += af * diff_f;
            }
            partial_tile[row][n] = acc;  // Phase A partial（= 硬件 bank2 口径，未乘 scale）
            // Phase B：统一乘一次 scale（bank5 存 fp16，fptofp→fp32 后 fpmul，故取 fp16 舍入值）。
            const float sc = fp16_roundtrip(scale_col[col]);
            c_tile[row][n] = acc * sc;
        }
    }
    // [MS_DBG] smc0 row0：打印 golden 未乘 scale 的 partial（硬件 bank2 口径），供与 TB bank2[0]
    // 对比（按 (3-n) 反序 = 硬件 fp32 lane 顺序）。定位是 partial(Chain1/2/3) 错还是 Phase B 累加错。
    if (smc_id == 0 && std::getenv("MM_DUMP_GOLDEN_BLOCKS")) {
        std::fprintf(stderr, "[MS_DBG][golden] smc0 kb%u row0 partial(unscaled) bank2-lane bits=", k_block);
        for (int n = 3; n >= 0; --n) {
            uint32_t b = 0; float v = partial_tile[0][static_cast<uint32_t>(n)];
            std::memcpy(&b, &v, sizeof(b));
            std::fprintf(stderr, "%08x ", b);
        }
        std::fprintf(stderr, "\n");
    }

    // bank7 写回布局【行主序】：word w = 第 w 行。
    // 硬件实测：bank 十六进制转储会把一个字的 16 字节整体倒序，故 write_fp32_le 的字节偏移
    // 0/4/8/12 分别落到硬件 fp32 lane 3/2/1/0；而硬件 bank2/bank7 的 fp32 lane 0..3 依次是
    // 列 n0(col0)/n1/n2/n3。为与硬件逐 lane 对齐，这里按 (3-n) 反序写入：
    //   offset 0/4/8/12 = c_tile[n3]/[n2]/[n1]/[n0]。
    bank2_out.assign(kGoldenRows * 16u, 0u);
    for (uint32_t row = 0; row < kGoldenRows; ++row) {
        const size_t base = static_cast<size_t>(row) * 16u;
        write_fp32_le(bank2_out, base + 0u, c_tile[row][3]);
        write_fp32_le(bank2_out, base + 4u, c_tile[row][2]);
        write_fp32_le(bank2_out, base + 8u, c_tile[row][1]);
        write_fp32_le(bank2_out, base + 12u, c_tile[row][0]);
    }
    return true;
}

bool MatmulNtW4f16Op::compute_bank2_golden_fp16_kchunk(uint8_t smc_id,
                                            const std::vector<float>& a,
                                            const std::vector<float>& b,
                                            uint32_t k_block,
                                            std::vector<uint8_t>& bank2_out,
                                            uint32_t n_stride,
                                            uint32_t col_base) {
    // A：8×K；B：K×n_stride fp16 稠密（无 zp/scale），本 case 取全局列
    // [col_base, col_base+32)。写回布局与 GPTQ Phase B 后 bank7 一致。
    const size_t kNCols = static_cast<size_t>(n_stride);
    if (a.empty() || (a.size() % kGoldenRows) != 0) return false;
    const size_t kTot = a.size() / kGoldenRows;
    if (b.size() < kTot * kNCols) return false;
    const uint32_t k_base = k_block * kGoldenKChunk;
    if (k_base + kGoldenKChunk > kTot) return false;

    std::array<std::array<float, kGoldenColsPerSmc>, kGoldenRows> c_tile{};
    for (uint32_t row = 0; row < kGoldenRows; ++row) {
        for (uint32_t n = 0; n < kGoldenColsPerSmc; ++n) {
            const uint32_t col = col_base + 4u * static_cast<uint32_t>(smc_id) + n;
            float acc = 0.0f;
            for (uint32_t k = k_base; k < k_base + kGoldenKChunk; ++k) {
                const float af =
                    fp16_roundtrip(a[static_cast<size_t>(row) * kTot + static_cast<size_t>(k)]);
                const float bf =
                    fp16_roundtrip(b[static_cast<size_t>(k) * kNCols + static_cast<size_t>(col)]);
                acc += af * bf;
            }
            c_tile[row][n] = acc;
        }
    }
    bank2_out.assign(kGoldenRows * 16u, 0u);
    for (uint32_t row = 0; row < kGoldenRows; ++row) {
        const size_t base = static_cast<size_t>(row) * 16u;
        write_fp32_le(bank2_out, base + 0u, c_tile[row][3]);
        write_fp32_le(bank2_out, base + 4u, c_tile[row][2]);
        write_fp32_le(bank2_out, base + 8u, c_tile[row][1]);
        write_fp32_le(bank2_out, base + 12u, c_tile[row][0]);
    }
    return true;
}

bool MatmulNtW4f16Op::compute_bank2_golden_m1_ktile(uint8_t smc_id,
                                           const std::vector<float>& a,
                                           const std::vector<uint8_t>& bq_u4,
                                           const std::vector<uint8_t>& zp_col,
                                           const std::vector<float>& scale_col,
                                           uint32_t k_block,
                                           std::vector<uint8_t>& bank2_out) {
    return compute_bank2_golden_m1_ktile(smc_id, a, bq_u4, zp_col, scale_col, k_block,
                                         /*group_size=*/0u, bank2_out);
}

bool MatmulNtW4f16Op::compute_bank2_golden_m1_ktile(uint8_t smc_id,
                                           const std::vector<float>& a,
                                           const std::vector<uint8_t>& bq_u4,
                                           const std::vector<uint8_t>& zp_all,
                                           const std::vector<float>& scale_all,
                                           uint32_t k_block,
                                           uint32_t group_size,
                                           std::vector<uint8_t>& bank2_out,
                                           uint32_t n_stride,
                                           uint32_t col_base) {
    const size_t kNCols = static_cast<size_t>(n_stride);   // 全局 N 宽（列窗 col_base）
    if (a.empty() || kNCols == 0u) return false;
    const size_t kTot = a.size();
    if (bq_u4.size() < kTot * kNCols) return false;
    const uint32_t k_base = k_block * kMatmulNtW4f16M1KTile;
    if (k_base + kMatmulNtW4f16M1KTile > kTot) return false;

    const uint32_t gsz = (group_size == 0u) ? kMatmulNtW4f16M1KTile : group_size;  // 0 → 整 tile 单组
    const size_t ngrp = (gsz == 0u) ? 1u : (scale_all.size() / kNCols);
    if (zp_all.size() < static_cast<size_t>(col_base) + 32u ||
        scale_all.size() < static_cast<size_t>(col_base) + 32u)
        return false;

    // 与 HW 同构：按 8 段 K32 算 unscaled partial，再 × 该段所属 group 的 scale 后累加。
    std::array<float, kGoldenColsPerSmc> scaled{};
    for (uint32_t n = 0; n < kGoldenColsPerSmc; ++n) {
        const uint32_t col = col_base + 4u * static_cast<uint32_t>(smc_id) + n;
        float acc = 0.0f;
        for (uint32_t r = 0; r < 8u; ++r) {
            const uint32_t k0 = k_base + r * 32u;
            const uint32_t g = (gsz == 0u) ? 0u : (k0 / gsz);
            const size_t gbase = (g < ngrp) ? (static_cast<size_t>(g) * kNCols) : 0u;
            const uint8_t zpv = static_cast<uint8_t>(zp_all[gbase + col] & 0xFu);
            const float scv = scale_all[gbase + col];
            float partial = 0.0f;
            for (uint32_t t = 0; t < 32u; ++t) {
                const uint32_t k = k0 + t;
                const float af = fp16_roundtrip(a[static_cast<size_t>(k)]);
                const int32_t diff =
                    static_cast<int32_t>(bq_u4[static_cast<size_t>(k) * kNCols + col] & 0xFu) -
                    static_cast<int32_t>(zpv);
                partial += af * fp16_roundtrip(static_cast<float>(diff));
            }
            acc += partial * fp16_roundtrip(scv);
        }
        scaled[n] = acc;
    }
    bank2_out.assign(16u, 0u);
    write_fp32_le(bank2_out, 0u, scaled[3]);
    write_fp32_le(bank2_out, 4u, scaled[2]);
    write_fp32_le(bank2_out, 8u, scaled[1]);
    write_fp32_le(bank2_out, 12u, scaled[0]);
    return true;
}

std::string MatmulNtW4f16Op::get_name() const { return "MatMul_NT_w4f16"; }

MatmulNtW4f16Params MatmulNtW4f16Op::parse_request(const app::AppOperatorRequest& req) const {
    MatmulNtW4f16Params p{};
    p.para_a_bank = get_i32_param<uint8_t>(req, "a_bank", get_i32_param<uint8_t>(req, "input_bank", 0));
    p.para_b_bank = get_i32_param<uint8_t>(req, "b_bank", get_i32_param<uint8_t>(req, "other_bank", 1));
    p.para_out_bank =
        get_i32_param<uint8_t>(req, "out_bank", get_i32_param<uint8_t>(req, "output_bank", 2));
    p.para_a_addr = get_i32_param<uint8_t>(req, "a_addr", get_i32_param<uint8_t>(req, "input_addr", 0));
    p.para_b_addr = get_i32_param<uint8_t>(req, "b_addr", get_i32_param<uint8_t>(req, "other_addr", 0));
    p.para_out_addr = get_i32_param<uint8_t>(req, "out_addr", get_i32_param<uint8_t>(req, "output_addr", 0));
    p.para_K = std::max<uint32_t>(4u, get_i32_param<uint32_t>(req, "K", req.para_dimensions.para_w));
    p.para_num_smc = 8u;

    // M：请求参数 / 维度 h / 环境变量 MM_M
    uint32_t m = get_i32_param<uint32_t>(req, "M", get_i32_param<uint32_t>(req, "m_rows", 0u));
    if (m == 0u) {
        const int h = req.para_dimensions.para_h;
        if (h == 1) m = 1u;
        else if (h == 8) m = 8u;
        else if (h == static_cast<int>(kMatmulNtW4f16M256)) m = kMatmulNtW4f16M256;
    }
    if (m == 0u) {
        if (const char* e = std::getenv("MM_M")) {
            const int vv = std::atoi(e);
            if (vv == 1 || vv == 8 || vv == static_cast<int>(kMatmulNtW4f16M256))
                m = static_cast<uint32_t>(vv);
        }
    }
    if (m == 0u) m = 8u;
    if (m != 1u && m != 8u && m != kMatmulNtW4f16M256) {
        throw std::runtime_error("Mm: M must be 1, 8 or 256");
    }
    p.para_M = m;
    // N 维：MM_N 总列数 / MM_N_TILE_START 本 case 的 tile 序号（每 tile 32 列）。
    if (const char* e = std::getenv("MM_N")) {
        const int v = std::atoi(e);
        if (v >= 32) p.para_N_total = static_cast<uint32_t>(v);
    }
    if (const char* e = std::getenv("MM_N_TILE_START")) {
        const int v = std::atoi(e);
        if (v >= 0) p.para_n_tile_idx = static_cast<uint32_t>(v);
    }
    if (p.para_M == 1u) {
        p.para_K_tile = get_i32_param<uint32_t>(req, "K_tile", kMatmulNtW4f16M1KTile);
        if (p.para_K_tile != kMatmulNtW4f16M1KTile) {
            throw std::runtime_error("MatMul_NT_w4f16 M=1: K_tile must be 256");
        }
        // GEMV 微内核 K 窗口用 K_tile；validate_k 放宽由 compile_gemv 侧处理
    } else {
        p.para_K_tile = kMatmulNtW4f16M8KChunk;
        validate_k(p.para_K);
    }

    // M=256 f16 epilogue：由 LSU 侧逐 stage 传入（ep_words>0 → 只走 epilogue 编译）。
    p.para_ep_words = get_i32_param<uint32_t>(req, "ep_words", 0u);
    p.para_ep_src_addr = get_i32_param<uint8_t>(req, "ep_src_addr", 0);
    p.para_ep_dst_addr = get_i32_param<uint8_t>(req, "ep_dst_addr", 0);
    p.para_ep_src_bank = get_i32_param<uint8_t>(req, "ep_src_bank", 7);
    p.para_ep_dst_bank = get_i32_param<uint8_t>(req, "ep_dst_bank", 6);

    // MatMul_NT_w4f16：GPTQ INT4 算子，量化形式固定为 true。
    // 不再读 quantized/quant_mode/MM_QUANT，避免与 LSU 侧口径分叉。
    p.para_quantized = true;
    return p;
}

void MatmulNtW4f16Op::validate_k(uint32_t k) {
    static const uint32_t k_allowed[] = {4, 8, 16, 32, 64};
    for (uint32_t x : k_allowed) if (k == x) return;
    throw std::runtime_error("Mm: K must be one of 4/8/16/32/64");
}

// 旧入口已废弃：M=1 直接走 compile()（ds→bank3 + 分段 B）。
OpInstructionsBase* MatmulNtW4f16Op::compile_gemv_m1(const app::AppOperatorRequest&, const MatmulNtW4f16Params&) {
    throw std::logic_error("MatmulNtW4f16Op::compile_gemv_m1 obsolete; use compile() with M=1");
}

OpInstructionsBase* MatmulNtW4f16Op::compile(const app::AppOperatorRequest& req) {
    const MatmulNtW4f16Params p = parse_request(req);
    if (p.para_ep_words > 0u) {
        return compile_f16_epilogue(req, p);
    }
    if (!p.para_quantized) {
        return compile_fp16_gemm(req, p);
    }
    return compile_core(req, p);
}

OpInstructionsBase* MatmulNtW4f16Op::compile_fp16_gemm(const app::AppOperatorRequest& req,
                                              const MatmulNtW4f16Params& p) {
    if (p.para_M != 8u) {
        throw std::runtime_error(
            "MatMul_NT_w4f16 fp16×fp16: only M=8 is supported in v1 (got M=" + std::to_string(p.para_M) + ")");
    }
    return compile_core(req, p);
}

OpInstructionsBase* MatmulNtW4f16Op::compile_core(const app::AppOperatorRequest& req, const MatmulNtW4f16Params& p) {
    const bool use_gptq = p.para_quantized;
    // FPGA sparse mask 下 s4567 只能接 shiftup_d。置位后：
    //   吐数仍写 bank3；回放走 UR→shiftdn→ds_t旁路(intlv_en)→shiftup→rtovr-23。
    // 未置位走原来的 bank3 UR→rtovr-23（dense）。
    const bool sparse_s4567 = (std::getenv("MM_SPARSE_S4567") != nullptr);
    // M=1：A 经 reshape 成假 8×32 后与 M=8 共用 ds→bank3→回放；
    // B 为 8 段 K32 叠放（每段 4 word/SMC），第 r 轮读 b_addr+r；
    // Phase B 把 8 行 partial 累加进 bank7[0]（1 word 输出）。
    // fp16×fp16：旁路链路二；B 经 UR_R2 分拍锁存到 s0..s3；Phase B 为 bank7+=bank2。
    if (!use_gptq && p.para_M != 8u) {
        throw std::runtime_error("MatMul_NT_w4f16 compile_core: fp16 path requires M=8");
    }
    const bool is_gemv_m1 = (p.para_M == 1u);
    // 首阶段固定执行 tile 口径：A(8x32) * B(32x32)，先覆盖 K=32 的累加窗口。
    const uint32_t rounds = 8u;
    // 8x32 A：计算次数相对 4x32 参考实现翻倍。参考 (mm 没有改scales)：rounds=4, kS01Rounds=16
    // (比例 4x)。当前 rounds=8，为保持同比例并覆盖 8 行 A 的完整归约，kS01Rounds=32。
    // cru_s0123 / fp16mul / 5to1 / rtovr-19..22 已用 loops+dly 压缩为单条指令，随 kS01Rounds 自动放大，
    // 指令长度不随翻倍增长。
    constexpr uint32_t kS01Rounds = 32u;
    // cru_intadd：按用户要求由 32 次(@4拍) 改为连续 128 次(@每拍)，起点不变、窗口向后延长。
    // 128 = kS01Rounds*kRoundStride，与 cru_s0123 每拍采样窗口一致。
    constexpr uint16_t kIntaddContFires = 128u;
    constexpr uint16_t kBcastChainDly = 7u;     // 1 broadcast + 7 拍链传递
    // 双 session：每 session 16 拍 ds_out，共需 32 拍 shiftup WRD；sparse 回放再加 32。
    const uint16_t kShiftUpBeats = sparse_s4567 ? 64u : 32u;
    // intlv_out=0：每 slot 8 拍；行间插入 kDsPaceGap（见 smi_ds_t PACE_*）
    // 链路三喂数节拍：s4567 逐拍喂数（第1~4拍分别 s4/s5/s6/s7）
    constexpr uint16_t kBeatGap = 1;
    // 每轮使用 s4/s5/s6/s7 四拍，按 A 行的四组(G0/G1/G2/G3)喂入。
    constexpr uint16_t kS4567Beats = 4;
    // 与 smi_ds_t.v PACE_* 锁定。sparse 回放不依赖空档，默认连续吐数。
    constexpr uint16_t kDsPaceBeats = kS4567Beats;
    constexpr uint16_t kDsPaceGap = 0u;
    constexpr uint16_t kDsPaceStride = static_cast<uint16_t>(kDsPaceBeats + kDsPaceGap);
    // 按用户要求的严格时序：
    // 1) 两条 shiftdn broadcast 连续两拍（bcast1/bcast2）
    // 2) UR_R1 在 bcast1 前一拍，UR_R2 在 bcast2 前一拍
    constexpr uint16_t kTile0Bcast = 17u;
    constexpr uint16_t kShiftdnBcastGap = 1u;
    constexpr uint16_t kTile1Bcast = static_cast<uint16_t>(kTile0Bcast + kShiftdnBcastGap);
    constexpr uint16_t kTile0Ur = static_cast<uint16_t>(kTile0Bcast - 2u);
    constexpr uint16_t kTile1Ur = static_cast<uint16_t>(kTile1Bcast - 2u);
    // rtovr34 路由窗口跟随各自读/播拍，保持一拍前建立、播拍有效
    constexpr uint16_t kTile0R34Start = kTile0Ur;
    constexpr uint16_t kTile1R34Start = kTile1Ur;
    constexpr uint16_t kLastCrdCycle =
        static_cast<uint16_t>(kTile1Bcast + kBcastChainDly);
    constexpr uint16_t kDsPostLastWrGap = 18;   // 末拍 CRD → 首拍 ds out（波形标定）
    constexpr uint16_t kDsXposeReady = 19;      // 双 slot 写满 + 转置（refill 间距）
    constexpr uint16_t kDsOutStart =
        static_cast<uint16_t>(kLastCrdCycle + kDsPostLastWrGap);
    constexpr uint16_t kRoundStride = static_cast<uint16_t>(kS4567Beats * kBeatGap);  // 4
    constexpr uint16_t kShiftupToRtovr23Delay = 1u;
    constexpr uint16_t kCacheWriteDelay = 1u;
    constexpr uint16_t kReplayStartGap = 4u;
    // 链路三 replay 相对 bank3 最后一次写入的额外安全余量。
    // GPTQ：还要给 dequant 4 拍读 + int8tofp16 流水留窗，用大余量 64。
    // fp16：无链路二，bank3 写完后只需短 settling，再拖 64 拍会让 B 的 ur_rdata_2
    // 看起来“算完了才开始读”（与 mul 虽仍对齐，但相对链路一过晚）。
    const uint16_t kChain3ExtraDelay = use_gptq ? 64u : 8u;
    constexpr uint8_t kBank3CacheWords = 32u;
    // smi_ds_t pair-session 硬约束：第二对 cri_vld 必须在 session1 的 hi_out_last_beat
    // 之后（session_active 归 0）才会真正启动新 session（见 smi_ds_t.v L141/L155）。
    // session1：16 个 data beat，行间插入 kDsPaceGap；最后一拍 wall = 3*stride + 3。
    constexpr uint16_t kInterSessionGap = 3u;  // 保守留 3 拍余量
    constexpr uint16_t kDsSessionBeats = 16u;
    const uint16_t kSession1LastOut = static_cast<uint16_t>(
        kDsOutStart +
        ((kDsSessionBeats / kDsPaceBeats - 1u) * kDsPaceStride + (kDsPaceBeats - 1u)));
    // ds_t2 必须晚于 session1 的 hi_out_last_beat；tile2 broadcast 再晚 2 拍（与 tile0 对齐 ds_t0=Bcast-2）。
    const uint16_t kTile2Bcast = static_cast<uint16_t>(
        kSession1LastOut + kInterSessionGap + 2u);
    const uint16_t kTile3Bcast = static_cast<uint16_t>(kTile2Bcast + kShiftdnBcastGap);
    const uint16_t kTile2Ur = static_cast<uint16_t>(kTile2Bcast - 2u);
    const uint16_t kTile3Ur = static_cast<uint16_t>(kTile3Bcast - 2u);
    // 第二轮 ds 吐数起点 = tile3 broadcast + 链传递 + post-gap，保持与 session1 一致的推导。
    const uint16_t kSecondDsOutStart = static_cast<uint16_t>(
        kTile3Bcast + kBcastChainDly + kDsPostLastWrGap);
    constexpr uint16_t kTiles = 1;
    const uint16_t kTileStride =
        static_cast<uint16_t>(rounds * kRoundStride + 48u);
    // 尾段预算需覆盖 Chain 3 延迟 + Phase A + Phase B (kPhaseBStride*rounds + fpmul/fpadd 流水尾)
    const uint16_t total_cycles =
        static_cast<uint16_t>(kSecondDsOutStart +
                              std::max<uint32_t>(kIntaddContFires,
                                                 std::max<uint32_t>(rounds, kS01Rounds) * kRoundStride) +
                              kTiles * 64u + 400u);
    // UR_R3：deq 读 zp(bank4)，Phase B 读 bank7 累加器旧值；末尾统一 build。
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ur_r3_timeline;
    // UR_R2：Phase B 读 scale(bank5)；末尾统一 build。
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ur_r2_timeline;
    // UR_R1：链路一 A 预装、链路三 bank3 replay、Phase B 读 bank2 partial；末尾统一 build。
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ur_r1_timeline;
    // VIN/VOUT 路由事件：dequant 与 Phase B 共用同一 IQ 文件（同 IQ_ID 不能 emit 两次）。
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr14;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr15;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_vin0;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_vin1;
    uint16_t deq_vin_win_start = 0;
    uint16_t deq_vin_win_end = 0;

    auto ds_beat_cycle = [&](uint16_t tile, uint32_t round, uint16_t beat_in_round) -> uint16_t {
        const uint32_t beat_global = round * kS4567Beats + beat_in_round;
        const uint16_t session = static_cast<uint16_t>(beat_global / 16u);
        const uint16_t beat_local = static_cast<uint16_t>(beat_global % 16u);
        const uint16_t row_in_sess = static_cast<uint16_t>(beat_local / kDsPaceBeats);
        const uint16_t beat_in_row = static_cast<uint16_t>(beat_local % kDsPaceBeats);
        const uint16_t paced =
            static_cast<uint16_t>(row_in_sess * kDsPaceStride + beat_in_row);
        const uint16_t sess_base = (session == 0u) ? kDsOutStart : kSecondDsOutStart;
        return static_cast<uint16_t>(sess_base + paced * kBeatGap + tile * kTileStride);
    };
    auto replay_base_cycle = [&](uint16_t tile) -> uint16_t {
        const uint16_t last_ds = ds_beat_cycle(tile, static_cast<uint32_t>(rounds - 1u),
                                               static_cast<uint16_t>(kS4567Beats - 1u));
        const uint16_t last_cache_wr =
            static_cast<uint16_t>(last_ds + kShiftupToRtovr23Delay + kCacheWriteDelay);
        return static_cast<uint16_t>(last_cache_wr + kReplayStartGap + kChain3ExtraDelay);
    };
    auto replay_beat_cycle = [&](uint16_t tile, uint32_t round, uint16_t beat_in_round) -> uint16_t {
        const uint32_t beat_global = round * kS4567Beats + beat_in_round;
        return static_cast<uint16_t>(replay_base_cycle(tile) + beat_global * kBeatGap);
    };

    // UR 读口按事件各自拼 UrCru（R1/R2/R3），不再保留全局 ur_r 模板；
    // R0（旧 bank B / bank6 旁路读）已不发射。
    const uint8_t kSmcIdEn = static_cast<uint8_t>(std::min<uint32_t>(31u, p.para_num_smc));
    const uint8_t vin_cru = static_cast<uint8_t>(((kSmcIdEn & 0x1Fu) << 1) | 1u);
    const auto vin_cru_bytes = serialize_code_msb(vin_cru, 7);

    // UR 写模板：port0 用于 Phase A(bank2)/Phase B(bank7)；port3 缓存写 bank3 时另配。
    // 旧 port2→bank6（dequant 中间写回）已旁路，不再配置。
    iq::UrCru ur_w{};
    ur_w.para_vld = true;
    ur_w.para_we[0] = true;
    ur_w.para_waddr[0].bank = p.para_out_bank;
    ur_w.para_waddr[0].addr_word = p.para_out_addr;
    ur_w.para_wbe[0] = 0xFFFFu;
    ur_w.para_w_smc_id_en[0] = kSmcIdEn;
    ur_w.para_we[1] = true;
    ur_w.para_waddr[1].bank = p.para_out_bank;
    ur_w.para_waddr[1].addr_word = static_cast<uint8_t>(p.para_out_addr + 1u);
    ur_w.para_wbe[1] = 0xFFFFu;
    ur_w.para_w_smc_id_en[1] = kSmcIdEn;

    iq::ShiftDownCru sd_bcast{};
    sd_bcast.para_vld = true;
    sd_bcast.para_broadcast = true;
    sd_bcast.para_smc_id = static_cast<uint8_t>(p.para_num_smc - 1u);
    const auto sd_bcast_code =
        serialize_code_msb(iq::ShiftDownIq::pack_cru_shiftdown(sd_bcast), 7);

    iq::SmiDsTCru ds{};
    ds.para_vld = true;
    ds.para_intlv_grp = false;   // slot0/slot1 pair
    ds.para_intlv_in = false;    // 非间插写：先写 slot0，slot0 ready 后写 slot1
    ds.para_intlv_out = false;   // 连续读：先读 slot0，再读 slot1

    iq::ShiftUpCru su{};
    su.para_vld = true;
    su.para_broadcast = true;
    su.para_smc_id = static_cast<uint8_t>(p.para_num_smc - 1u);
    std::memset(su.para_data, 0, sizeof(su.para_data));

    const uint8_t rtovr34_r1_code = static_cast<uint8_t>((31u << 1) | 1u);  // ur_rdata_1 -> shiftdown
    // 【Phase A：链路三 partial 直写 bank2】
    // 8to1_d(input28) -> uvr_wdata0(bank2, addr 0~7)。sparse mask 允许 input28，不经 VOUT。
    // 【Phase B：Post-K 累加】USE_RTOVR_SPARSE_MASK 禁止 UR→FPMUL/FPADD 与 FPADD_D→UVR_WDATA，
    // 与 pow 相同走 VIN/VOUT 中转：
    //   bank2(ur_rdata1) -> VIN0（整轮不改挂）-> fpmul_s0
    //   scale(ur_rdata2) -> VIN1 -> fptofp -> fpmul_s1
    //   bank7(ur_rdata3) -> VIN1(scale 采完后改挂) -> fpadd_s0
    //   fpmul_d -> fpadd_s1；fpadd_d -> VOUT0 -> uvr_wdata0(bank7)
    const uint8_t rtovr35_at8_d_code = static_cast<uint8_t>((28u << 1) | 1u);
    // 在线反量化（sparse：UR 不能进 INTADD，经 VIN）：
    //   zp(ur_rdata3) -> rtovr-14 -> VIN0 -> rtovr-0/1 -> intadd s0/s1
    //   q (ur_rdata2) -> rtovr-15 -> VIN1 -> rtovr-2   -> intadd s2
    const uint8_t rtovr14_zp_to_vin0 = static_cast<uint8_t>((33u << 1) | 1u);
    const uint8_t rtovr15_q_to_vin1 = static_cast<uint8_t>((32u << 1) | 1u);
    const uint8_t rtovr0_zp_to_intadd_s0 = static_cast<uint8_t>((11u << 1) | 1u);  // VIN0_D
    const uint8_t rtovr1_zp_to_intadd_s1 = static_cast<uint8_t>((11u << 1) | 1u);
    const uint8_t rtovr2_q_to_intadd_s2 = static_cast<uint8_t>((12u << 1) | 1u);   // VIN1_D
    // B：GPTQ 走 int8tofp16 d0~d3(input14..17)；fp16 走 ur_rdata2(input32) 分拍锁存到 s0..s3。
    const uint8_t rtovr19_code = use_gptq ? static_cast<uint8_t>((14u << 1) | 1u)
                                          : static_cast<uint8_t>((32u << 1) | 1u);
    const uint8_t rtovr20_code = use_gptq ? static_cast<uint8_t>((15u << 1) | 1u)
                                          : static_cast<uint8_t>((32u << 1) | 1u);
    const uint8_t rtovr21_code = use_gptq ? static_cast<uint8_t>((16u << 1) | 1u)
                                          : static_cast<uint8_t>((32u << 1) | 1u);
    const uint8_t rtovr22_code = use_gptq ? static_cast<uint8_t>((17u << 1) | 1u)
                                          : static_cast<uint8_t>((32u << 1) | 1u);
    // 链路二（反量化）：rtovr17/18 接 intadd→int8tofp16；旧 rtovr37→bank6 写回已删除。
    const uint8_t rtovr17_intadd_d0_to_i8fp16_s0 = static_cast<uint8_t>((0u << 1) | 1u);
    const uint8_t rtovr18_intadd_d1_to_i8fp16_s1 = static_cast<uint8_t>((1u << 1) | 1u);
    // RtoVR 关闭需发一拍 vld=1 才会锁存新值；选保留零输入(34)实现强制清零。
    const uint8_t rtovr_disable_to_zero_code = static_cast<uint8_t>((34u << 1) | 1u);
    // 链路二 A 回放：ur_rdata_1(input31) 经 rtovr23 进 dvr_fp16mul_s4567（dense）。
    // sparse 只允许 shiftup_d(input29)；ds 吐数时先经 shiftup 灌一轮 s4567，replay 再按行覆盖。
    const uint8_t rtovr23_code = static_cast<uint8_t>((31u << 1) | 1u);
    const uint8_t rtovr23_shiftup_code = static_cast<uint8_t>((29u << 1) | 1u);
    // 链路一 A 备份：dr_shiftup_d(input29) 经 rtovr38 写入 uvr_wdata_3。
    const uint8_t rtovr38_shiftup_code = static_cast<uint8_t>((29u << 1) | 1u);
    const auto rtovr19_code_bytes = serialize_code_msb(rtovr19_code, 7);
    const auto rtovr20_code_bytes = serialize_code_msb(rtovr20_code, 7);
    const auto rtovr21_code_bytes = serialize_code_msb(rtovr21_code, 7);
    const auto rtovr22_code_bytes = serialize_code_msb(rtovr22_code, 7);
    const auto rtovr23_code_bytes = serialize_code_msb(rtovr23_code, 7);
    const auto rtovr23_shiftup_code_bytes = serialize_code_msb(rtovr23_shiftup_code, 7);
    const auto rtovr38_shiftup_code_bytes = serialize_code_msb(rtovr38_shiftup_code, 7);
    const auto rtovr0_code_bytes = serialize_code_msb(rtovr0_zp_to_intadd_s0, 7);
    const auto rtovr1_code_bytes = serialize_code_msb(rtovr1_zp_to_intadd_s1, 7);
    const auto rtovr2_code_bytes = serialize_code_msb(rtovr2_q_to_intadd_s2, 7);
    const auto rtovr17_code_bytes = serialize_code_msb(rtovr17_intadd_d0_to_i8fp16_s0, 7);
    const auto rtovr18_code_bytes = serialize_code_msb(rtovr18_intadd_d1_to_i8fp16_s1, 7);
    const auto rtovr_disable_to_zero_code_bytes = serialize_code_msb(rtovr_disable_to_zero_code, 7);
    // rtovr24/25 分别驱动 addtree s0/s1（对齐 RTL 端口映射）

    iq::Fp16MulCru f16{};
    f16.para_mul_valid = true;
    f16.para_mul_src0_enc = 0;
    f16.para_mul_src1_enc = 0;
    f16.para_smc_id_en = kSmcIdEn;
    f16.para_s0123_valid = true;
    f16.para_s0123_high = 0;
    f16.para_s4567_valid = true;
    f16.para_s4567_high = 0;
    f16.para_s4567_low = 0;
    // fp16mul bank 策略：
    // - S0123: 固定写入 bank0
    // - S4567: 仅初始化写入 bank0 的 s4/s5/s6/s7 各一次，后续保持不变
    // - calc : 固定按 bank0-bank0 计算
    std::vector<uint8_t> f16_calc_bank0_code;
    std::vector<uint8_t> f16s0123_bank0_code;
    std::array<std::vector<uint8_t>, 4> f16s4567_bank0_codes{};
    {
        auto t = f16;
        t.para_mul_src0_enc = 0u;
        t.para_mul_src1_enc = 0u;
        t.para_s0123_high = 0u;
        t.para_s4567_high = 0u;
        f16_calc_bank0_code = serialize_code_msb(iq::Fp16MulIq::pack_cru_fp16mul_calc(t), 8);
        f16s0123_bank0_code = serialize_code_msb(iq::Fp16MulIq::pack_cru_fp16mul_s0123(t), 7);
    }
    for (uint8_t low = 0; low < 4; ++low) {
        auto t_low = f16;
        t_low.para_s4567_high = 0u;  // 固定写 bank0
        t_low.para_s4567_low = low;
        f16s4567_bank0_codes[low] =
            serialize_code_msb(iq::Fp16MulIq::pack_cru_fp16mul_s4567(t_low), 9);
    }

    iq::Fp32AddTree5To1Cru at_first{};
    at_first.para_inst_valid = true;
    at_first.para_accumulate = false;
    at_first.para_smc_id_en = kSmcIdEn;
    const auto at_first_code =
        serialize_code_msb(iq::Fp32AddTree5To1Iq::pack_cru_fp32addtree5to1(at_first), 7);
    iq::Fp32AddTree8To1Cru at8{};
    at8.para_inst_valid = true;
    at8.para_smc_id_en = kSmcIdEn;
    std::array<std::vector<uint8_t>, 4> at8_codes{};
    for (uint8_t i = 0; i < 4; ++i) {
        auto t = at8;
        // 按用户要求：先写高32位，再依次往低位写。
        t.para_dst_reg_enc = static_cast<uint8_t>(3u - i);
        at8_codes[i] = serialize_code_msb(iq::Fp32AddTree8To1Iq::pack_cru_fp32addtree8to1(t), 8);
    }
    iq::IntAddCru intadd_q_zp{};
    intadd_q_zp.para_inst_valid = true;
    intadd_q_zp.para_precision_s0 = 0u;  // 8-bit
    intadd_q_zp.para_precision_s1 = 0u;  // 8-bit
    intadd_q_zp.para_precision_s2 = 0u;  // 8-bit (src2 nibble expand)
    intadd_q_zp.para_sign_s0 = false;    // zp uint4->int8模板
    intadd_q_zp.para_sign_s1 = false;
    intadd_q_zp.para_sign_s2 = false;    // q uint4
    intadd_q_zp.para_sign_d = true;      // 输出允许负值（q-zp）
    intadd_q_zp.para_update_st = false;
    intadd_q_zp.para_is_sub = true;      // src2 - src_base => q - zp
    intadd_q_zp.para_smc_id_en = kSmcIdEn;
    const auto intadd_q_zp_code =
        serialize_code_msb(iq::IntAddIq::pack_cru_intadd(intadd_q_zp), 18);
    iq::Int8ToFp16Cru i8fp16{};
    i8fp16.para_inst_valid = true;
    i8fp16.para_signed_mode = true;
    i8fp16.para_smc_id_en = kSmcIdEn;
    const auto int8tofp16_code =
        serialize_code_msb(iq::Int8ToFp16Iq::pack_cru_int8tofp16(i8fp16), 7);
    iq::FpMulCru fp32_fpmul{};
    fp32_fpmul.para_inst_valid = true;
    fp32_fpmul.para_src_precision_is_32b = true;   // fp32 * fp32
    fp32_fpmul.para_dst_precision_is_32b = true;   // fp32
    fp32_fpmul.para_smc_id_en = kSmcIdEn;
    const auto fp32_fpmul_code = serialize_code_msb(iq::FpMulIq::pack_cru_fpmul(fp32_fpmul), 8);

    auto out = std::make_unique<MatmulNtW4f16Instructions>();
    out->para_total_steps = total_cycles;
    out->para_total_elements = 64;
    constexpr uint16_t kDeqBatches = 4u;  // B 的 4 个 group（每 group = 4 个 128b lane -> int8tofp16 d0~d3）
    constexpr uint16_t kDeqBatchStride = 4u;  // 保留：仅用于 fp16_chain_offset 常量
    constexpr uint16_t kDeqBaseLag = 2u;
    constexpr uint16_t kDeqI8Fp16Delay = 4u;  // 读地址拍到 int8tofp16 触发拍（旧标定）
    constexpr uint16_t kIntaddRtovrDelay = 2u;  // read -> intadd 采样偏移（老能出数版本标定值）
    // int8tofp16 相对 intadd 的触发偏移；保持与历史标定一致（=2）。
    constexpr uint16_t kInt8AfterIntadd = static_cast<uint16_t>(kDeqI8Fp16Delay - kIntaddRtovrDelay);
    // GPTQ B(q)：latency=2（历史标定）。
    // fp16 B：IQ→smi_top CRU(+1)→UR BRAM/re_d(+1)→再留 1 拍让 ur_rdata 在 RtoVR 采样沿前稳定。
    // （INSTQ 溢出修好后若仍数值不对，可用波形核对 lat=2/3；当前保持 3。）
    const uint16_t kUrReadDataLatency = use_gptq ? 2u : 3u;
    constexpr uint16_t kRtovrLat = 1u;            // RtoVR OUTPUT_PIPE_STAGE
    constexpr uint16_t kVinLat = 2u;              // 进 VIN：RtoVR 管 1 + IPR/OPR 1
    constexpr uint16_t kRtoVr3Base = 6u;         // int8tofp16 d 相对读地址拍的可路由延迟
    constexpr uint16_t kS4567RoundStride = 16u;  // 一轮 16 拍 = kDsPaceStride（4 吐 + 12 空）
    // s0-s3 采样基准（与后文 rtovr-19..22 的 s01_base 完全一致，见 kS01RtovrDelay/kS01DvrExtraDelay）。
    constexpr uint16_t kS01RtovrDelay = 1u;      // UR 同步读后 RtoVR 下一拍采样
    constexpr uint16_t kS01DvrExtraDelay = 1u;   // dvr 采样对齐
    // 反量化读地址拍 -> int8tofp16 d 有效。GPTQ 多 VIN 两拍，读提前、intadd 采样对齐旧锚点。
    const uint16_t kDeqPipeLatency =
        use_gptq ? static_cast<uint16_t>(kRtoVr3Base + kVinLat) : kRtoVr3Base;
    // GPTQ：为反量化流水垫一段 offset。
    // fp16：无 intadd/int8tofp16，offset 只留极小 settling，让 B 读与 s4567/calc 紧贴 replay 起点。
    const uint16_t fp16_chain_offset =
        use_gptq ? static_cast<uint16_t>(kDeqBaseLag + kDeqBatchStride * kDeqBatches + 16u)
                 : 2u;
    // sparse：bypass data_out → RtoVR → s4567。seed42 K=32 扫 lat：10 采早两拍（0/8），
    // 12 为 DDR hex 8/8；11/13 又偏一拍。可用 MM_SPARSE_BYPASS_LAT 覆盖。
    const uint16_t kSparseBypassLat = []() -> uint16_t {
        if (const char* e = std::getenv("MM_SPARSE_BYPASS_LAT")) {
            const int v = std::atoi(e);
            if (v >= 0 && v <= 32) return static_cast<uint16_t>(v);
        }
        return 12u;
    }();
    // s4567 / rtovr-23 / chain3 再提前 1 拍：让 extra[0] 进 dvr，且下一行 s4 与上一行
    // 最后一次 calc 同拍（NBA 仍用旧 S4）。
    auto chain3_cycle = [&](uint16_t tile, uint32_t s, uint16_t beat_in_group) -> uint16_t {
        uint16_t c = static_cast<uint16_t>(
            replay_beat_cycle(tile, s, beat_in_group) + fp16_chain_offset);
        if (sparse_s4567) {
            c = static_cast<uint16_t>(c + kSparseBypassLat);
            if (c > 0u) c = static_cast<uint16_t>(c - 1u);
        }
        return c;
    };
    const uint16_t deq_s01_sample_base = static_cast<uint16_t>(
        chain3_cycle(0u, 0u, 0u) + kS01RtovrDelay + kS01DvrExtraDelay);
    const uint16_t fp16_b_rtovr_base = deq_s01_sample_base;
    auto deq_read_cycle = [&](uint32_t r) -> uint16_t {
        return static_cast<uint16_t>(chain3_cycle(0u, r, 0u) + kS01RtovrDelay +
                                     kS01DvrExtraDelay - kDeqPipeLatency);
    };
    const uint16_t deq_first_read = deq_read_cycle(0u);
    const uint16_t intadd_cont_start = static_cast<uint16_t>(
        deq_first_read + kIntaddRtovrDelay + (use_gptq ? kVinLat : 0u));
    const uint16_t deq_disable_after_intadd =
        static_cast<uint16_t>(intadd_cont_start + kIntaddContFires + kRtoVr3Base);
    const uint8_t zp_bank = get_i32_param<uint8_t>(req, "zp_bank", 4);
    const uint8_t zp_addr = get_i32_param<uint8_t>(req, "zp_addr", 0);
    const uint8_t scale_bank = get_i32_param<uint8_t>(req, "scale_bank", 5);
    const uint8_t scale_addr_base = get_i32_param<uint8_t>(req, "scale_addr", 0);
    const uint32_t k_block = get_i32_param<uint32_t>(req, "k_block", 0);
    const uint32_t group_size = std::max<uint32_t>(1u, get_i32_param<uint32_t>(req, "group_size", 64));
    // LSU 把当前 group 的 scale/zp DMA 到 scale_addr / zp_addr（batch 多槽时为 local_group*4）。
    // M=8：deq 读 zp_addr+0..3；Phase B 读 scale_addr（word0 含本 SMC 四列 scale）。
    // M=1：K_tile 内 4 个 GPTQ group 预载到 zp/scale 槽 0..3；行 r 用 group=r/2。
    (void)k_block;
    const uint8_t scale_addr = scale_addr_base;
    // 每 group 覆盖的 K32 段数（group_size=64 → 2 段/组）
    const uint32_t segs_per_group =
        std::max<uint32_t>(1u, group_size / 32u);

    // UR_RDATA2 / 链路二：GPTQ 走反量化；fp16 分拍读 bank1 的 4 个 K 组 word，经 RtoVR 保持锁存到 s0..s3。
    if (use_gptq) {
        // M=8：第 r 轮读 group (r % 4)（同一 K32 的 4 列循环）。
        // M=1：第 r 轮读 b_addr+r（8 段×4 列顺序：row=r/4 的 B[32r:32r+32]）。
        for (uint32_t r = 0; r < kS01Rounds; ++r) {
            const uint16_t g = is_gemv_m1 ? static_cast<uint16_t>(r)
                                         : static_cast<uint16_t>(r % kDeqBatches);
            iq::UrCru ur_r_q2{};
            ur_r_q2.para_vld = true;
            ur_r_q2.para_re[2] = true;
            ur_r_q2.para_raddr[2].bank = p.para_b_bank;
            ur_r_q2.para_raddr[2].addr_word = static_cast<uint8_t>(p.para_b_addr + g);
            ur_r_q2.para_r_smc_id_en[2] = kSmcIdEn;
            ur_r2_timeline.push_back(
                {deq_read_cycle(r),
                 serialize_code_msb(extract_ur_read_port_code17(ur_r_q2, 2), 17)});
        }
        // UR_RDATA3：dequant 读 zp(bank4)
        iq::UrCru ur_r_zp{};
        ur_r_zp.para_vld = true;
        ur_r_zp.para_re[3] = true;
        ur_r_zp.para_raddr[3].bank = zp_bank;
        ur_r_zp.para_raddr[3].addr_word = zp_addr;
        ur_r_zp.para_r_smc_id_en[3] = kSmcIdEn;
        for (uint32_t r = 0; r < kS01Rounds; ++r) {
            const uint16_t batch_base = deq_read_cycle(r);
            const uint8_t kg = static_cast<uint8_t>(r % kDeqBatches);
            const uint8_t row = static_cast<uint8_t>(r / kDeqBatches);
            const uint8_t grp = is_gemv_m1
                                    ? static_cast<uint8_t>(row / segs_per_group)
                                    : static_cast<uint8_t>(0u);
            auto ur_tmp = ur_r_zp;
            ur_tmp.para_raddr[3].bank = zp_bank;
            ur_tmp.para_raddr[3].addr_word =
                static_cast<uint8_t>(zp_addr + grp * kDeqBatches + kg);
            ur_r3_timeline.push_back({batch_base,
                                      serialize_code_msb(extract_ur_read_port_code17(ur_tmp, 3), 17)});
        }
    } else {
        // ---- fp16 dense B 喂数（独立实现，不走 GPTQ deq_read_cycle / int8tofp16 窗）----
        // bank1 布局：addr = b_addr + col + 4*g；第 r 轮 col=r%4，拍 g=0..3 读同列 4 个 K 组。
        // 每拍只开 rtovr(19+g) 锁存到 s0..s3；UR 读拍默认 = rtovr - kUrReadDataLatency。
        // kFp16UrR2PostDelay：整段 UR_R2 相对该默认再后移若干拍（rtovr/s0123 不动）。
        //
        // 压缩：不可 128 条互异 READ（INSTQ_DEPTH=128 会回绕，尾部 NOP 盖到队头导致整体滞后）。
        // 每轮地址为 col,col+4,col+8,col+12 → 1 条 (loops=3, inc=+4)；32 轮共 32 条 + 前后 NOP/END。
        constexpr uint16_t kFp16BGroups = 4u;
        constexpr int8_t kFp16BAddrInc = 4;
        constexpr uint16_t kFp16UrR2PostDelay = 2u;  // ur_rdata_2 整体后移 2 拍
        {
            IqInstructionFile f;
            f.para_iq_id = iq::IQ_ID_UR_RDATA_2;
            f.para_iq_name = iq::IQ_NAME_UR_RDATA_2;
            f.para_width_bits = 41u + 17u;
            const auto zero17 = serialize_code_msb(0, 17);
            uint16_t ur2_cur = 0;
            auto ur2_nop_to = [&](uint16_t t) {
                if (t > ur2_cur) {
                    append_nop_loops(f.para_data, 17, static_cast<uint16_t>(t - ur2_cur), zero17);
                    ur2_cur = t;
                }
            };
            for (uint32_t r = 0; r < kS01Rounds; ++r) {
                const uint16_t t_read = static_cast<uint16_t>(
                    chain3_cycle(0u, r, 0u) + kS01RtovrDelay + kS01DvrExtraDelay -
                    kUrReadDataLatency + kFp16UrR2PostDelay);
                ur2_nop_to(t_read);
                const uint8_t col = static_cast<uint8_t>(r % kFp16BGroups);
                iq::UrCru ur_r_b{};
                ur_r_b.para_vld = true;
                ur_r_b.para_re[2] = true;
                ur_r_b.para_raddr[2].bank = p.para_b_bank;
                ur_r_b.para_raddr[2].addr_word =
                    static_cast<uint8_t>(p.para_b_addr + col);
                ur_r_b.para_r_smc_id_en[2] = kSmcIdEn;
                const auto code =
                    serialize_code_msb(extract_ur_read_port_code17(ur_r_b, 2), 17);
                append_active_repeat_inc(f.para_data, 17, code, kFp16BGroups, 1u, kFp16BAddrInc);
                ur2_cur = static_cast<uint16_t>(ur2_cur + kFp16BGroups);
            }
            if (total_cycles > ur2_cur) {
                append_nop_loops(f.para_data, 17,
                                 static_cast<uint16_t>(total_cycles - ur2_cur), zero17);
            }
            auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero17, 17);
            f.para_data.insert(f.para_data.end(), end.begin(), end.end());
            out->para_iq_files.push_back(std::move(f));
        }
        // ur_r2_timeline 留空：本路径已直接 emit；末尾 build_iq_timeline 跳过空表。
    }
    // UR_RDATA2(scale) 移到写回段参与 bank2_old + partial*scale，不在 deq 段读取。
    // 反量化输入路由（仅 GPTQ）：
    auto build_deq_rtovr_window = [&](uint32_t iq_id, const std::string& iq_name,
                                      const std::vector<uint8_t>& on_code,
                                      uint16_t disable_cycle,
                                      uint16_t start_offset) {
        IqInstructionFile f;
        f.para_iq_id = iq_id;
        f.para_iq_name = iq_name;
        f.para_width_bits = static_cast<uint32_t>(41 + 7);
        const auto zero7 = serialize_code_msb(0, 7);
        auto append_idle = [&](uint16_t loops) {
            if (loops == 0u) return;
            auto idle = make_instruction(0, 0, 0, static_cast<uint16_t>(loops - 1u), 0, 0, 0, 0, 0,
                                         zero7, 7);
            f.para_data.insert(f.para_data.end(), idle.begin(), idle.end());
        };
        // RtoVR 输出为寄存器保持：仅在 vld=1 的拍更新。
        // 因此窗口内需要连续 vld=1，确保每拍都抓到源端最新数据，而不是保持旧值。
        const uint16_t window_start = static_cast<uint16_t>(deq_first_read + start_offset);
        append_idle(window_start);
        uint16_t cur = window_start;
        if (disable_cycle > window_start) {
            append_active_repeat(f.para_data, 7, on_code,
                                 static_cast<uint16_t>(disable_cycle - window_start), 1u);
            cur = disable_cycle;
        }
        append_active(f.para_data, 7, rtovr_disable_to_zero_code_bytes);
        ++cur;
        if (total_cycles > cur) append_idle(static_cast<uint16_t>(total_cycles - cur));
        auto end = make_instruction(1, 1, 0, 0, 0, 0, 0, 0, 0, zero7, 7);
        f.para_data.insert(f.para_data.end(), end.begin(), end.end());
        out->para_iq_files.push_back(std::move(f));
    };
    if (use_gptq) {
    // zp/q 经 VIN 后再进 intadd：RtoVR-0/1/2 等 VIN_D，比 UR 就绪再晚 kVinLat。
    build_deq_rtovr_window(iq::IQ_ID_RTOVR_BASE + 0, "rtovr-0", rtovr0_code_bytes,
                           deq_disable_after_intadd,
                           static_cast<uint16_t>(kUrReadDataLatency + kVinLat));
    build_deq_rtovr_window(iq::IQ_ID_RTOVR_BASE + 1, "rtovr-1", rtovr1_code_bytes,
                           deq_disable_after_intadd,
                           static_cast<uint16_t>(kUrReadDataLatency + kVinLat));
    build_deq_rtovr_window(iq::IQ_ID_RTOVR_BASE + 2, "rtovr-2", rtovr2_code_bytes,
                           deq_disable_after_intadd,
                           static_cast<uint16_t>(kUrReadDataLatency + kVinLat));
    {
        const uint16_t t_deq_vin =
            static_cast<uint16_t>(deq_first_read + kUrReadDataLatency);
        deq_vin_win_start = t_deq_vin;
        deq_vin_win_end = deq_disable_after_intadd;
        ev_vin0.push_back({t_deq_vin, vin_cru_bytes});
        ev_vin1.push_back({t_deq_vin, vin_cru_bytes});
    }
    // intadd(q-zp)：连续 128 拍。
    build_iq_timeline_single_burst(
        iq::IQ_ID_INTADD, iq::IQ_NAME_INTADD, 18,
        intadd_cont_start, kIntaddContFires, 1u,
        intadd_q_zp_code, total_cycles, out->para_iq_files);
    // intadd -> int8tofp16 输入路由（全 deq 窗口）
    build_deq_rtovr_window(iq::IQ_ID_RTOVR_BASE + 17, "rtovr-17", rtovr17_code_bytes,
                           deq_disable_after_intadd, 0u);
    build_deq_rtovr_window(iq::IQ_ID_RTOVR_BASE + 18, "rtovr-18", rtovr18_code_bytes,
                           deq_disable_after_intadd, 0u);
    // int8tofp16：起点对齐 intadd 结果拍，但保持连续 valid（避免间歇触发导致首拍/中间拍掉数）。
    // 【直连】d0~d3 不再写回 bank6，改由 rtovr-19..22 直接路由到 fp16mul s0~s3（见后文）。
    {
        const uint16_t first_intadd_cyc = intadd_cont_start;
        const uint16_t last_intadd_cyc =
            static_cast<uint16_t>(intadd_cont_start + kIntaddContFires - 1u);
        const uint16_t i8_start = static_cast<uint16_t>(first_intadd_cyc + kInt8AfterIntadd);
        const uint16_t i8_end =
            static_cast<uint16_t>(last_intadd_cyc + kInt8AfterIntadd + 2u);
        const uint16_t i8_repeats = static_cast<uint16_t>(i8_end - i8_start + 1u);
        build_iq_timeline_single_burst(
            iq::IQ_ID_INT8_TO_FP16, iq::IQ_NAME_INT8_TO_FP16, 7,
            i8_start, i8_repeats, 1u, int8tofp16_code, total_cycles, out->para_iq_files);
    }
    }  // use_gptq 链路二
    // A 侧：UR_RDATA_1 早期读取 A（供 shiftdn），后期回放 A'（供 rtovr23->s4567），末段 Phase B 读 bank2。
    {
        auto& ur1_ev = ur_r1_timeline;  // 引用函数级 timeline，Phase B 之后统一 build
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> r34_ev;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> sd_ev;
        // A 侧 shiftdn 预装改走 ur_rdata_1：两段链路一分别读 addr0/1 与 addr2/3。
        {
            iq::UrCru ur_tmp{};
            ur_tmp.para_vld = true;
            ur_tmp.para_re[1] = true;
            ur_tmp.para_raddr[1].bank = p.para_a_bank;
            ur_tmp.para_raddr[1].addr_word = static_cast<uint8_t>(p.para_a_addr + 0u);
            ur_tmp.para_r_smc_id_en[1] = kSmcIdEn;
            ur1_ev.push_back({kTile0Ur, serialize_code_msb(extract_ur_read_port_code17(ur_tmp, 1), 17)});
        }
        {
            iq::UrCru ur_tmp{};
            ur_tmp.para_vld = true;
            ur_tmp.para_re[1] = true;
            ur_tmp.para_raddr[1].bank = p.para_a_bank;
            ur_tmp.para_raddr[1].addr_word = static_cast<uint8_t>(p.para_a_addr + 1u);
            ur_tmp.para_r_smc_id_en[1] = kSmcIdEn;
            ur1_ev.push_back({kTile1Ur, serialize_code_msb(extract_ur_read_port_code17(ur_tmp, 1), 17)});
        }
        {
            iq::UrCru ur_tmp{};
            ur_tmp.para_vld = true;
            ur_tmp.para_re[1] = true;
            ur_tmp.para_raddr[1].bank = p.para_a_bank;
            ur_tmp.para_raddr[1].addr_word = static_cast<uint8_t>(p.para_a_addr + 2u);
            ur_tmp.para_r_smc_id_en[1] = kSmcIdEn;
            ur1_ev.push_back({kTile2Ur, serialize_code_msb(extract_ur_read_port_code17(ur_tmp, 1), 17)});
        }
        {
            iq::UrCru ur_tmp{};
            ur_tmp.para_vld = true;
            ur_tmp.para_re[1] = true;
            ur_tmp.para_raddr[1].bank = p.para_a_bank;
            ur_tmp.para_raddr[1].addr_word = static_cast<uint8_t>(p.para_a_addr + 3u);
            ur_tmp.para_r_smc_id_en[1] = kSmcIdEn;
            ur1_ev.push_back({kTile3Ur, serialize_code_msb(extract_ur_read_port_code17(ur_tmp, 1), 17)});
        }
        // 链路二 A 回放：从 bank3 经 ur_rdata_1 读回。
        // dense：rtovr23 直连 s4567；sparse：同一地址经 shiftdn→ds_t旁路→shiftup→rtovr23。
        for (uint32_t r = 0; r < rounds; ++r) {
            for (uint16_t i = 0; i < 4u; ++i) {
                // 对齐 cru_fp16mul_s4567：s4567 数据链路整体后移 1 拍，避免 dvr 早到一拍。
                const uint16_t read_cyc = static_cast<uint16_t>(
                    replay_beat_cycle(0u, 0u, i) + fp16_chain_offset + 1u +
                    static_cast<uint16_t>(r * kS4567RoundStride));
                const uint8_t addr =
                    static_cast<uint8_t>((rounds - 1u - r) + i * rounds);
                iq::UrCru ur_tmp{};
                ur_tmp.para_vld = true;
                ur_tmp.para_re[1] = true;
                ur_tmp.para_raddr[1].bank = 3u;
                ur_tmp.para_raddr[1].addr_word = addr;
                ur_tmp.para_r_smc_id_en[1] = kSmcIdEn;
                ur1_ev.push_back(
                    {read_cyc, serialize_code_msb(extract_ur_read_port_code17(ur_tmp, 1), 17)});
                if (sparse_s4567) {
                    const uint16_t t_crd = static_cast<uint16_t>(read_cyc + kUrReadDataLatency);
                    sd_ev.push_back({t_crd, sd_bcast_code});
                }
            }
        }
        // 严格按“UR前一拍 + bcast当拍”组织两段路由窗口。
        const uint16_t r34_r1_end = kTile0Bcast;
        for (uint16_t r34_cyc = kTile0R34Start; r34_cyc <= r34_r1_end; ++r34_cyc) {
            r34_ev.push_back({r34_cyc, serialize_code_msb(rtovr34_r1_code, 7)});
        }
        const uint16_t r34_r2_start = kTile1R34Start;
        const uint16_t r34_r2_end = kTile1Bcast;
        for (uint16_t r34_cyc = r34_r2_start; r34_cyc <= r34_r2_end; ++r34_cyc) {
            r34_ev.push_back({r34_cyc, serialize_code_msb(rtovr34_r1_code, 7)});
        }
        for (uint16_t r34_cyc = kTile2Ur; r34_cyc <= kTile2Bcast; ++r34_cyc) {
            r34_ev.push_back({r34_cyc, serialize_code_msb(rtovr34_r1_code, 7)});
        }
        for (uint16_t r34_cyc = kTile3Ur; r34_cyc <= kTile3Bcast; ++r34_cyc) {
            r34_ev.push_back({r34_cyc, serialize_code_msb(rtovr34_r1_code, 7)});
        }
        if (sparse_s4567) {
            // INSTQ_DEPTH=128：回放窗口按拍展开会回绕，队尾 NOP 盖掉吐数段的 UR→shiftdn，
            // ds_t SRAM 被全 0 灌满。吐数起到回放结束用一条 loops hold。
            const uint16_t r34_rep_hi = static_cast<uint16_t>(
                replay_beat_cycle(0u, 0u, kS4567Beats - 1u) + fp16_chain_offset + 1u +
                static_cast<uint16_t>((rounds - 1u) * kS4567RoundStride) + kUrReadDataLatency);
            const uint16_t r34_hold_end =
                static_cast<uint16_t>(std::min<uint32_t>(
                    static_cast<uint32_t>(r34_rep_hi) + 1u, total_cycles));
            build_iq_valid_hold_window(
                iq::IQ_ID_RTOVR_BASE + 34, "rtovr-34", 7,
                serialize_code_msb(rtovr34_r1_code, 7), kTile0R34Start, r34_hold_end,
                total_cycles, out->para_iq_files);
        } else {
            build_iq_timeline(iq::IQ_ID_RTOVR_BASE + 34, "rtovr-34", 7, r34_ev, total_cycles,
                              out->para_iq_files);
        }
        // 吐数：与 dense 相同，只在 4 个 bcast 拍打 broadcast（链上自行传递）。
        // sparse 回放的 32 个 gather 拍也塞进同一份 timeline（中间 code=0 的 IQ nop）。
        sd_ev.push_back({kTile0Bcast, sd_bcast_code});
        sd_ev.push_back({kTile1Bcast, sd_bcast_code});
        sd_ev.push_back({kTile2Bcast, sd_bcast_code});
        sd_ev.push_back({kTile3Bcast, sd_bcast_code});
        build_iq_timeline(iq::IQ_ID_SHIFTDN, iq::IQ_NAME_SHIFTDN, 7, sd_ev, total_cycles,
                          out->para_iq_files);
    }
    {
        // IQ93：mm_shift build_iq_timeline；loops+vld=1+code=0 插空拍，T0/T1 在 broadcast 前 2 拍开 slot。
        const uint16_t ds_t0_cyc =
            (kTile0Bcast >= 2u) ? static_cast<uint16_t>(kTile0Bcast - 2u) : 0u;
        const uint16_t ds_t1_cyc = static_cast<uint16_t>(ds_t0_cyc + 1u);
        const uint16_t ds_t2_cyc =
            (kTile2Bcast >= 2u) ? static_cast<uint16_t>(kTile2Bcast - 2u) : 0u;
        const uint16_t ds_t3_cyc = static_cast<uint16_t>(ds_t2_cyc + 1u);
        iq::SmiDsTCru ds_open = ds;
        const auto code_open =
            serialize_code_msb(iq::SmiDsTIq::pack_cru_smi_ds_t(ds_open), iq::kSmiDsTCodeBits);
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ds_ev = {
            {ds_t0_cyc, code_open}, {ds_t1_cyc, code_open},
            {ds_t2_cyc, code_open}, {ds_t3_cyc, code_open}};
        if (sparse_s4567) {
            iq::SmiDsTCru ds_bypass{};
            ds_bypass.para_vld = true;
            ds_bypass.para_intlv_en = true;
            const auto code_bypass =
                serialize_code_msb(iq::SmiDsTIq::pack_cru_smi_ds_t(ds_bypass),
                                   iq::kSmiDsTCodeBits);
            // 与 shiftdn 同拍的 CRD 常晚 1 拍；只开 t_crd+1，避免 t_crd∪(t_crd+1) 在行内连拍上多灌。
            for (uint32_t r = 0; r < rounds; ++r) {
                for (uint16_t i = 0; i < 4u; ++i) {
                    const uint16_t read_cyc = static_cast<uint16_t>(
                        replay_beat_cycle(0u, 0u, i) + fp16_chain_offset + 1u +
                        static_cast<uint16_t>(r * kS4567RoundStride));
                    const uint16_t t_crd = static_cast<uint16_t>(read_cyc + kUrReadDataLatency);
                    ds_ev.push_back({static_cast<uint16_t>(t_crd + 1u), code_bypass});
                }
            }
        }
        build_iq_timeline(iq::IQ_ID_SMI_DS_T, iq::IQ_NAME_SMI_DS_T, iq::kSmiDsTCodeBits,
                          ds_ev, total_cycles, out->para_iq_files);
    }
    // 对齐 tb_aiacc_top_shift：IQ14 预填 WRD 队列，不做人为固定延迟；
    // 实际发射由 smi_ds_t.wrd_vld 门控（EXTRA_DATA）决定。
    build_shiftup_iq14_wrd_queue(su, kShiftUpBeats, out->para_iq_files);
    // 先把 ds_t->shiftup 数据备份到 UR bank3，后续链路二通过 ur_rdata_1 回放到 s4~s7。
    // 优化：A 在 tile 间复用，仅缓存 16 个 word（减半写回），回放按地址取模循环复用。
    {
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr38;
        std::vector<std::pair<uint16_t, uint8_t>> cache_w3_events;  // <cycle, addr>
        constexpr uint16_t kCacheTile = 0u;
        const uint8_t base_addr = 0u;
        for (uint32_t r = 0; r < rounds; ++r) {
            for (uint16_t i = 0; i < kS4567Beats; ++i) {
                const uint32_t beat_global = r * kS4567Beats + i;
                if (beat_global >= kBank3CacheWords) continue;
                const uint16_t src_cyc =
                    static_cast<uint16_t>(ds_beat_cycle(kCacheTile, r, i) + kShiftupToRtovr23Delay);
                ev_rtovr38.push_back({src_cyc, rtovr38_shiftup_code_bytes});
                const uint8_t addr = static_cast<uint8_t>(base_addr + r * kS4567Beats + i);
                cache_w3_events.push_back(
                    {static_cast<uint16_t>(src_cyc + kCacheWriteDelay), addr});
            }
        }
        build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 38, "rtovr-38", 7, ev_rtovr38,
                                       total_cycles, out->para_iq_files);

        IqInstructionFile f;
        f.para_iq_id = iq::IQ_ID_UR_WDATA_3;
        f.para_iq_name = iq::IQ_NAME_UR_WDATA_3;
        f.para_width_bits = 74;
        std::sort(cache_w3_events.begin(), cache_w3_events.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        auto append_w3_nop = [&](uint32_t loops) {
            if (loops == 0) return;
            while (loops > 1024u) {
                auto nop = serialize_ur_w_inst_33(1, 0, 0, 1023, 0, 0, 0, 0, 0, 0);
                f.para_data.insert(f.para_data.end(), nop.begin(), nop.end());
                loops -= 1024u;
            }
            auto nop = serialize_ur_w_inst_33(1, 0, 0, static_cast<uint16_t>(loops - 1u), 0, 0, 0, 0,
                                              0, 0);
            f.para_data.insert(f.para_data.end(), nop.begin(), nop.end());
        };
        uint16_t cur = 0;
        for (const auto& ev : cache_w3_events) {
            if (ev.first > cur) {
                append_w3_nop(static_cast<uint32_t>(ev.first - cur));
                cur = ev.first;
            }
            auto ur_w3 = ur_w;
            ur_w3.para_we[0] = false;
            ur_w3.para_we[1] = false;
            ur_w3.para_we[3] = true;
            ur_w3.para_waddr[3].bank = 3u;
            ur_w3.para_waddr[3].addr_word = ev.second;
            ur_w3.para_wbe[3] = 0xFFFFu;
            ur_w3.para_w_smc_id_en[3] = kSmcIdEn;
            const uint64_t code = extract_ur_write_port_code33(ur_w3, 3);
            auto wr = serialize_ur_w_inst_33(1, 0, 0, 0, 0, kUrWdataActiveDly, 0, 0, 0, code);
            f.para_data.insert(f.para_data.end(), wr.begin(), wr.end());
            cur = static_cast<uint16_t>(cur + 1u);
        }
        if (total_cycles > cur) {
            append_w3_nop(static_cast<uint32_t>(total_cycles - cur));
        }
        // end 指令显式置 jump_last（IIC 以 inst[41:40]==2'b11 判定），避免队列尾部重复发射
        auto end = serialize_ur_w_inst_33(1, 1, 0, 0, 0, 0, 6, 0, 0, 0);
        f.para_data.insert(f.para_data.end(), end.begin(), end.end());
        out->para_iq_files.push_back(std::move(f));
    }
    // S3 compute stage — 锚定 ds 吐数窗口，5to1 期间下一轮回载 s4567（round_stride=8）。
    constexpr uint16_t off_fp16_calc = 0u;
    constexpr uint16_t off_addtree_calc = 9u;
    constexpr uint16_t kAddtree5Advance = 3u;
    // 链路二错拍基准：s0 在 beat0，s1 在 beat1；两路都到位以后按 beat1 计算。
    constexpr uint16_t kS01IssueBeat = 3u;
    // 注：kS01RtovrDelay / kS01DvrExtraDelay 已上移到反量化块前定义（s0-s3 直连需共用同一采样基准）。
    constexpr uint16_t kS0123CruExtraDelay = 1u;
    constexpr uint16_t kS01ReadyExtraOffset = 0u;
    // S4567 保持原有触发拍点不变；当 S0~S3 提前时，给 S4567 额外补 1 拍。
    constexpr uint16_t kS4567IssueExtraDelay = 1u;
    constexpr uint16_t kCruS01S4567Delay = 1u;
    constexpr uint16_t kRtovr23OpenDelay = 2u;    // 相对 replay+offset 的开窗偏移
    constexpr uint16_t kRtovr23WindowBeats = 4u;  // 每轮覆盖 s4/s5/s6/s7 四拍装载
    constexpr uint16_t kRtovr23TailGuard = 2u;    // 尾窗保护，覆盖最后一路稳定采样
    constexpr uint16_t kCalcAfterS01Beats = 3u;  // 计算触发整体后移 1 拍
    // 计算开始时序在当前基础上再前调 2 拍（fp16mul / 5to1 / 8to1 同步调整）。
    constexpr uint16_t kComputeDelay = 0u;

    const uint8_t rtovr24_d0_code = static_cast<uint8_t>((18u << 1) | 1u);  // dr_fp16mul_d0 -> addtree_s0
    const uint8_t rtovr25_d1_code = static_cast<uint8_t>((19u << 1) | 1u);  // dr_fp16mul_d1 -> addtree_s1
    const uint8_t rtovr26_d2_code = static_cast<uint8_t>((20u << 1) | 1u);  // dr_fp16mul_d2 -> addtree_s2
    const uint8_t rtovr27_d3_code = static_cast<uint8_t>((21u << 1) | 1u);  // dr_fp16mul_d3 -> addtree_s3
    const uint8_t rtovr28_d4_code = static_cast<uint8_t>((22u << 1) | 1u);  // dr_fp16mul_d4 -> addtree_s4
    const uint8_t rtovr29_d5_code = static_cast<uint8_t>((23u << 1) | 1u);  // dr_fp16mul_d5 -> addtree_s5
    const uint8_t rtovr30_d6_code = static_cast<uint8_t>((24u << 1) | 1u);  // dr_fp16mul_d6 -> addtree_s6
    const uint8_t rtovr31_d7_code = static_cast<uint8_t>((25u << 1) | 1u);  // dr_fp16mul_d7 -> addtree_s7
    // 5to1 -> 8to1：d0/d1 分别送到 dvr_fp32addtree8to1_s0/s1（RtoVR 输出 32/33）
    const uint8_t rtovr32_5to1d0_code =
        static_cast<uint8_t>((26u << 1) | 1u);  // dr_fp32addtree5to1_d0 -> addtree8to1_s0
    const uint8_t rtovr33_5to1d1_code =
        static_cast<uint8_t>((27u << 1) | 1u);  // dr_fp32addtree5to1_d1 -> addtree8to1_s1

    // 每 4 轮 8to1 拼 1 个 128b word；写回轮为每个 macro 末轮。
    constexpr uint32_t kWbMacroGroupSize = 4u;
    const uint16_t kWbWordsPerTile = static_cast<uint16_t>(rounds);
    const uint16_t kWbWords = static_cast<uint16_t>(kTiles * kWbWordsPerTile);
    // 8to1 发码拍 + DR 输出延迟，与旧 rtovr-35 直连写回标定一致。
    constexpr uint16_t kAt8DrOutLat = 7u;
    auto at8_dr_d_valid_cycle = [&](uint16_t tile, uint32_t at8_round) -> uint16_t {
        const uint16_t s01_ready = static_cast<uint16_t>(
            chain3_cycle(tile, at8_round, kS01IssueBeat) + kS01RtovrDelay + kS01ReadyExtraOffset);
        const uint16_t comp =
            static_cast<uint16_t>(s01_ready + kCalcAfterS01Beats + kComputeDelay);
        const uint16_t at8_fire = static_cast<uint16_t>(comp + off_addtree_calc + 4u);
        return static_cast<uint16_t>(at8_fire + kAt8DrOutLat);
    };

    {
        // cru_s0123：
        // GPTQ：128 拍连发（与 int8tofp16 连续吐数对齐）。
        // fp16：每轮仅在 4 组都锁存后发 1 拍（s0..s3 同拍从 4 路 DVR 写入）。
        if (use_gptq) {
            const uint16_t s0123_start = static_cast<uint16_t>(
                chain3_cycle(0u, 0u, 0u) + kS01RtovrDelay + kCruS01S4567Delay +
                kS0123CruExtraDelay);
            const uint16_t s0123_repeats = static_cast<uint16_t>(kS01Rounds * kS4567Beats);
            build_iq_timeline_single_burst(iq::IQ_ID_FP16MUL_S0123, iq::IQ_NAME_FP16MUL_S0123, 7,
                                           s0123_start, s0123_repeats, 1u,
                                           f16s0123_bank0_code, total_cycles,
                                           out->para_iq_files);
        } else {
            std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev;
            for (uint32_t r = 0; r < kS01Rounds; ++r) {
                const uint16_t cyc = static_cast<uint16_t>(
                    chain3_cycle(0u, r, 0u) + kS01RtovrDelay + kS01DvrExtraDelay + 4u);
                ev.push_back({cyc, f16s0123_bank0_code});
            }
            build_iq_timeline_idle_invalid(iq::IQ_ID_FP16MUL_S0123, iq::IQ_NAME_FP16MUL_S0123, 7,
                                           ev, total_cycles, out->para_iq_files);
        }
    }
    {
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev;
        for (uint16_t tile = 0; tile < kTiles; ++tile) {
            for (uint32_t r = 0; r < rounds; ++r) {
                for (uint16_t i = 0; i < kS4567Beats; ++i) {
                    uint16_t s4567_cyc = static_cast<uint16_t>(
                        replay_beat_cycle(tile, 0u, i) + fp16_chain_offset +
                        static_cast<uint16_t>(r * kS4567RoundStride) + kS01RtovrDelay +
                        kCruS01S4567Delay + kS4567IssueExtraDelay +
                        (sparse_s4567 ? kSparseBypassLat : 0u));
                    if (sparse_s4567 && s4567_cyc > 0u)
                        s4567_cyc = static_cast<uint16_t>(s4567_cyc - 1u);
                    ev.push_back(std::make_pair(
                        s4567_cyc, f16s4567_bank0_codes[static_cast<size_t>(i & 0x3u)]));
                }
            }
        }
        build_iq_timeline(iq::IQ_ID_FP16MUL_S4567, iq::IQ_NAME_FP16MUL_S4567, 9, ev,
                          total_cycles, out->para_iq_files);
    }
    {
        // fp16mul: 首次计算为起点，之后每 kRoundStride(4) 拍一次，共 kS01Rounds(32) 次。
        // 用显式离散事件（vld=1 单拍触发 + vld=1 空拍间隔）保证 4 拍间隔被 RTL 严格执行，
        // 而不是依赖 single_burst 的 dly（避免 dly 未生效导致连续触发）。
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev;
        for (uint32_t r = 0; r < kS01Rounds; ++r) {
            const uint16_t cyc = static_cast<uint16_t>(
                chain3_cycle(0u, r, kS01IssueBeat) + kS01RtovrDelay + kS01ReadyExtraOffset +
                kCalcAfterS01Beats + kComputeDelay + off_fp16_calc);
            ev.push_back({cyc, f16_calc_bank0_code});
        }
        build_iq_timeline(iq::IQ_ID_FP16MUL, iq::IQ_NAME_FP16MUL, 8, ev,
                          total_cycles, out->para_iq_files);
    }
    {
        // rtovr-19/20/21/22：
        // GPTQ：int8tofp16 d0~d3 同拍 → 连续 128 拍。
        // fp16：单口 UR 分拍喂 in32；第 r 轮 beat g 只开 rtovr(19+g)，靠 RtoVR 保持拼齐 s0..s3。
        const uint16_t s01_base = use_gptq ? deq_s01_sample_base : fp16_b_rtovr_base;
        struct RtovrDesc { uint32_t id; const char* name; const std::vector<uint8_t>* code; };
        const RtovrDesc descs[4] = {
            {iq::IQ_ID_RTOVR_BASE + 19u, "rtovr-19", &rtovr19_code_bytes},
            {iq::IQ_ID_RTOVR_BASE + 20u, "rtovr-20", &rtovr20_code_bytes},
            {iq::IQ_ID_RTOVR_BASE + 21u, "rtovr-21", &rtovr21_code_bytes},
            {iq::IQ_ID_RTOVR_BASE + 22u, "rtovr-22", &rtovr22_code_bytes},
        };
        if (use_gptq) {
            for (const auto& d : descs) {
                build_iq_timeline_single_burst(d.id, d.name, 7, s01_base,
                                               kIntaddContFires, 1u,
                                               *d.code, total_cycles, out->para_iq_files);
            }
        } else {
            for (uint16_t g = 0; g < 4u; ++g) {
                std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev;
                for (uint32_t r = 0; r < kS01Rounds; ++r) {
                    const uint16_t cyc = static_cast<uint16_t>(
                        chain3_cycle(0u, r, 0u) + kS01RtovrDelay + kS01DvrExtraDelay + g);
                    ev.push_back({cyc, *descs[g].code});
                }
                build_iq_timeline_idle_invalid(descs[g].id, descs[g].name, 7, ev,
                                               total_cycles, out->para_iq_files);
            }
        }
    }
    {
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev;
        const auto& r23_code = sparse_s4567 ? rtovr23_shiftup_code_bytes : rtovr23_code_bytes;
        for (uint32_t r = 0; r < rounds; ++r) {
            const uint16_t burst_start = static_cast<uint16_t>(
                replay_beat_cycle(0u, 0u, 0u) + fp16_chain_offset + kRtovr23OpenDelay +
                static_cast<uint16_t>(r * kS4567RoundStride) +
                (sparse_s4567 ? kSparseBypassLat : 0u) -
                (sparse_s4567 ? 1u : 0u));
            const uint16_t burst_end = static_cast<uint16_t>(
                burst_start + kRtovr23WindowBeats + kRtovr23TailGuard);
            for (uint16_t cyc = burst_start; cyc < burst_end; ++cyc) {
                ev.push_back({cyc, r23_code});
            }
            ev.push_back({burst_end, rtovr_disable_to_zero_code_bytes});
        }
        build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 23, "rtovr-23", 7, ev,
                                       total_cycles, out->para_iq_files);
    }
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 24, "rtovr-24", 7,
                           serialize_code_msb(rtovr24_d0_code, 7), total_cycles, out->para_iq_files);
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 25, "rtovr-25", 7,
                           serialize_code_msb(rtovr25_d1_code, 7), total_cycles, out->para_iq_files);
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 26, "rtovr-26", 7,
                           serialize_code_msb(rtovr26_d2_code, 7), total_cycles, out->para_iq_files);
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 27, "rtovr-27", 7,
                           serialize_code_msb(rtovr27_d3_code, 7), total_cycles, out->para_iq_files);
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 28, "rtovr-28", 7,
                           serialize_code_msb(rtovr28_d4_code, 7), total_cycles, out->para_iq_files);
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 29, "rtovr-29", 7,
                           serialize_code_msb(rtovr29_d5_code, 7), total_cycles, out->para_iq_files);
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 30, "rtovr-30", 7,
                           serialize_code_msb(rtovr30_d6_code, 7), total_cycles, out->para_iq_files);
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 31, "rtovr-31", 7,
                           serialize_code_msb(rtovr31_d7_code, 7), total_cycles, out->para_iq_files);
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 32, "rtovr-32", 7,
                           serialize_code_msb(rtovr32_5to1d0_code, 7), total_cycles,
                           out->para_iq_files);
    build_iq_constant_hold(iq::IQ_ID_RTOVR_BASE + 33, "rtovr-33", 7,
                           serialize_code_msb(rtovr33_5to1d1_code, 7), total_cycles,
                           out->para_iq_files);
    {
        // 5to1: 首次为起点，之后每 kRoundStride(4) 拍一次，共 kS01Rounds(32) 次。
        // 显式离散事件（vld=1 单拍 + vld=1 空拍间隔），保证 4 拍间隔被 RTL 严格执行、
        // 跨满整个 128 拍计算窗口（与 fp16mul/8to1 一致），避免 single_burst 的 dly 塌成连续。
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev5;
        for (uint32_t r = 0; r < kS01Rounds; ++r) {
            const uint16_t cyc = static_cast<uint16_t>(
                chain3_cycle(0u, r, kS01IssueBeat) + kS01RtovrDelay + kS01ReadyExtraOffset +
                kCalcAfterS01Beats + kComputeDelay + off_addtree_calc - kAddtree5Advance);
            ev5.push_back({cyc, at_first_code});
        }
        build_iq_timeline(iq::IQ_ID_FP32_ADDTREE_5TO1, iq::IQ_NAME_FP32_ADDTREE_5TO1, 7, ev5,
                          total_cycles, out->para_iq_files);
    }
    {
        // 8to1 改为两拍一次，跟随 5to1 输出窗口
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev;
        for (uint16_t tile = 0; tile < kTiles; ++tile) {
            for (uint32_t r = 0; r < kS01Rounds; ++r) {
                const uint16_t s01_ready_cycle = static_cast<uint16_t>(
                    chain3_cycle(tile, r, kS01IssueBeat) + kS01RtovrDelay + kS01ReadyExtraOffset);
                const uint16_t comp_base =
                    static_cast<uint16_t>(s01_ready_cycle + kCalcAfterS01Beats);
                const uint16_t comp =
                    static_cast<uint16_t>(comp_base + kComputeDelay);
                ev.push_back(std::make_pair(
                    static_cast<uint16_t>(comp + off_addtree_calc + 4u),
                    at8_codes[static_cast<size_t>(r & 0x3u)]));
            }
        }
        build_iq_timeline(iq::IQ_ID_FP32_ADDTREE_8TO1, iq::IQ_NAME_FP32_ADDTREE_8TO1, 8, ev,
                          total_cycles, out->para_iq_files);
    }
    {
        // 【Phase A】8to1_d(input28) -> rtovr-35 -> uvr_wdata0(bank2)。sparse 允许 input28。
        // 【Phase B】bank7 += bank2 × scale。sparse mask 下 UR 不能进 FPMUL/FPADD，
        // FPADD_D 不能进 UVR_WDATA，必须经 VIN/VOUT（与 pow 相同）：
        //   ur_rdata1(bank2) -> rtovr-14 -> VIN0（整轮不改挂）-> rtovr-3 -> fpmul_s0
        //   ur_rdata2(scale) -> rtovr-15 -> VIN1 -> rtovr-7 -> fptofp -> rtovr-4 -> fpmul_s1
        //   ur_rdata3(bank7) -> rtovr-15 -> VIN1(scale 采完后改挂) -> rtovr-5 -> fpadd_s0
        //   fpmul_d -> rtovr-6 -> fpadd_s1
        //   fpadd_d -> rtovr-16 -> VOUT0 -> rtovr-35 -> uvr_wdata0(bank7)
        constexpr uint16_t kAccUrWGap = 1u;                 // 8to1_d / VOUT_D 到 wdata 采样 1 拍余量
        constexpr uint16_t kAccFpmulLat = 4u;               // fpmul in→d
        constexpr uint16_t kAccFpaddLat = 4u;               // fpadd in→d
        constexpr uint16_t kPhaseBGuard = 4u;               // Phase A 末写与 Phase B 起点安全间隔
        // VIN0 整轮挂 bank2。VIN1 先挂 scale，等 fpmul 采完 s1 再改挂 bank7。
        // 一轮写回约 t_read+17；stride=10 会把写回叠进下一轮，统一用 20。
        const uint16_t kPhaseBStride = 20u;

        // RtoVR code = (src_id << 1) | vld
        const uint8_t rtovr14_ur1_to_vin0 = static_cast<uint8_t>((31u << 1) | 1u);  // bank2
        const uint8_t rtovr14_ur3_to_vin0 = static_cast<uint8_t>((33u << 1) | 1u);  // bank7 复用 VIN0
        const uint8_t rtovr15_ur2_to_vin1 = static_cast<uint8_t>((32u << 1) | 1u);  // scale
        const uint8_t rtovr15_ur3_to_vin1 = static_cast<uint8_t>((33u << 1) | 1u);  // fp16: bank7
        const uint8_t rtovr3_vin0_to_fpmul_s0 = static_cast<uint8_t>((11u << 1) | 1u);
        const uint8_t rtovr7_vin1_to_fptofp = static_cast<uint8_t>((12u << 1) | 1u);
        const uint8_t rtovr4_fptofp_to_fpmul_s1 = static_cast<uint8_t>((6u << 1) | 1u);
        const uint8_t rtovr5_vin0_to_fpadd_s0 = static_cast<uint8_t>((11u << 1) | 1u);
        const uint8_t rtovr5_vin1_to_fpadd_s0 = static_cast<uint8_t>((12u << 1) | 1u);
        const uint8_t rtovr6_fpmul_d_to_fpadd_s1 = static_cast<uint8_t>((3u << 1) | 1u);
        const uint8_t rtovr6_vin0_to_fpadd_s1 = static_cast<uint8_t>((11u << 1) | 1u);
        const uint8_t rtovr16_fpadd_d_to_vout0 = static_cast<uint8_t>((4u << 1) | 1u);
        const uint8_t rtovr35_vout_to_wdata0 = static_cast<uint8_t>((13u << 1) | 1u);

        // fptofp CRU：fp16->fp32（src_pos/dst_pos=低位），smc_id_en 广播全 SMC。
        iq::FpToFpCru fptofp_cru{};
        fptofp_cru.para_inst_valid = true;
        fptofp_cru.para_src_is_32b = false;  // 源 fp16
        fptofp_cru.para_dst_is_32b = true;   // 目的 fp32
        fptofp_cru.para_src_high = false;
        fptofp_cru.para_dst_high = false;
        fptofp_cru.para_smc_id_en = kSmcIdEn;
        const auto fptofp_code =
            serialize_code_msb(iq::FpToFpIq::pack_cru_fptofp(fptofp_cru), 10);

        iq::FpAddCru fpadd_cru{};
        fpadd_cru.para_inst_valid = true;
        fpadd_cru.para_mode_is_32b = true;
        fpadd_cru.para_smc_id_en = kSmcIdEn;
        const auto fpadd_active_code =
            serialize_code_msb(iq::FpAddIq::pack_cru_fpadd(fpadd_cru), 10);

        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr5;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr6;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr35;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr3_acc;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr4_acc;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr7_acc;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr16;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_vout0;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_fptofp;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_fpmul_acc;
        std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_fpadd;

        using WbEvent = UrWbEvent;
        std::vector<WbEvent> wb_events;
        wb_events.reserve(static_cast<size_t>(kWbWords) * 2u);

        // -------- Phase A：8to1_d → bank2[addr] --------
        uint16_t phase_a_last_wr = 0u;
        for (uint16_t tile = 0; tile < kTiles; ++tile) {
            for (uint32_t wb = 0; wb < rounds; ++wb) {
                const uint32_t at8_round = wb * kWbMacroGroupSize + (kWbMacroGroupSize - 1u);
                const uint16_t t_at8_d = at8_dr_d_valid_cycle(tile, at8_round);
                const uint8_t addr = static_cast<uint8_t>(
                    p.para_out_addr + static_cast<uint8_t>(tile * rounds + wb));

                ev_rtovr35.push_back({t_at8_d, serialize_code_msb(rtovr35_at8_d_code, 7)});
                const uint16_t t_ur_w = static_cast<uint16_t>(t_at8_d + kAccUrWGap);
                wb_events.push_back({t_ur_w, addr, p.para_out_bank});
                if (t_ur_w > phase_a_last_wr) phase_a_last_wr = t_ur_w;
            }
        }

        // -------- Phase B --------
        // GPTQ：bank7[addr] = bank7[addr] + bank2[addr] × scale
        // fp16：bank7[addr] = bank7[addr] + bank2[addr]（无 scale）
        // M=1：8 行 partial 累加进 bank7[out_addr]（1 word）。
        const uint16_t phase_b_base = static_cast<uint16_t>(phase_a_last_wr + kPhaseBGuard);
        const uint8_t bank7_out_bank = 7u;
        for (uint16_t tile = 0; tile < kTiles; ++tile) {
            for (uint32_t wb = 0; wb < rounds; ++wb) {
                const uint8_t bank2_addr = static_cast<uint8_t>(
                    p.para_out_addr + static_cast<uint8_t>(tile * rounds + wb));
                const uint8_t acc_addr =
                    is_gemv_m1 ? p.para_out_addr
                               : static_cast<uint8_t>(p.para_out_addr +
                                                     static_cast<uint8_t>(tile * rounds + wb));

                const uint16_t t_read = static_cast<uint16_t>(
                    phase_b_base + static_cast<uint16_t>((tile * rounds + wb) * kPhaseBStride));

                if (use_gptq) {
                    const uint16_t t_ur_r1_bank2 = t_read;
                    const uint16_t t_ur_r2_scale =
                        (t_read >= 2u) ? static_cast<uint16_t>(t_read - 2u) : 0u;
                    const uint16_t t_vin0_bank2 =
                        static_cast<uint16_t>(t_ur_r1_bank2 + kUrReadDataLatency);
                    const uint16_t t_vin1_scale =
                        static_cast<uint16_t>(t_ur_r2_scale + kUrReadDataLatency);
                    const uint16_t t_vin0_d =
                        static_cast<uint16_t>(t_vin0_bank2 + kVinLat);
                    const uint16_t t_vin1_d_scale =
                        static_cast<uint16_t>(t_vin1_scale + kVinLat);
                    const uint16_t t_route_sc2fp = t_vin1_d_scale;
                    const uint16_t t_fptofp = static_cast<uint16_t>(t_vin1_d_scale + kRtovrLat);
                    const uint16_t t_route_mul = t_vin0_d;
                    const uint16_t t_fpmul = static_cast<uint16_t>(t_route_mul + kRtovrLat);
                    const uint16_t t_fpmul_d = static_cast<uint16_t>(t_fpmul + kAccFpmulLat);
                    // VIN0 专供 bank2→fpmul_s0。bank7 改走 VIN1：等 fpmul 采完 s1（scale/fptofp）再改挂。
                    const uint16_t t_vin1_bank7 = static_cast<uint16_t>(t_fpmul + kAccFpmulLat);
                    const uint16_t t_ur_r3_bank7 =
                        (t_vin1_bank7 >= kUrReadDataLatency)
                            ? static_cast<uint16_t>(t_vin1_bank7 - kUrReadDataLatency) : 0u;
                    const uint16_t t_vin1_d_b7 =
                        static_cast<uint16_t>(t_vin1_bank7 + kVinLat);
                    const uint16_t t_route_add =
                        (t_fpmul_d > t_vin1_d_b7) ? t_fpmul_d : t_vin1_d_b7;
                    const uint16_t t_fpadd = static_cast<uint16_t>(t_route_add + 2u);
                    const uint16_t t_fpadd_d = static_cast<uint16_t>(t_fpadd + kAccFpaddLat);
                    const uint16_t t_vout = t_fpadd_d;
                    const uint16_t t_rtovr35_b = static_cast<uint16_t>(t_vout + kVinLat);
                    const uint16_t t_ur_w_b = static_cast<uint16_t>(t_rtovr35_b + kAccUrWGap);

                    iq::UrCru ur_r1_b2{};
                    ur_r1_b2.para_vld = true;
                    ur_r1_b2.para_re[1] = true;
                    ur_r1_b2.para_raddr[1].bank = p.para_out_bank;
                    ur_r1_b2.para_raddr[1].addr_word = bank2_addr;
                    ur_r1_b2.para_r_smc_id_en[1] = kSmcIdEn;
                    ur_r1_timeline.push_back(
                        {t_ur_r1_bank2, serialize_code_msb(extract_ur_read_port_code17(ur_r1_b2, 1), 17)});

                    const uint8_t scale_col_word =
                        is_gemv_m1
                            ? static_cast<uint8_t>((wb / segs_per_group) * kDeqBatches)
                            : static_cast<uint8_t>(0u);
                    iq::UrCru ur_r2_sc{};
                    ur_r2_sc.para_vld = true;
                    ur_r2_sc.para_re[2] = true;
                    ur_r2_sc.para_raddr[2].bank = scale_bank;
                    ur_r2_sc.para_raddr[2].addr_word =
                        static_cast<uint8_t>(scale_addr + scale_col_word);
                    ur_r2_sc.para_r_smc_id_en[2] = kSmcIdEn;
                    ur_r2_timeline.push_back(
                        {t_ur_r2_scale, serialize_code_msb(extract_ur_read_port_code17(ur_r2_sc, 2), 17)});

                    iq::UrCru ur_r3_b7{};
                    ur_r3_b7.para_vld = true;
                    ur_r3_b7.para_re[3] = true;
                    ur_r3_b7.para_raddr[3].bank = bank7_out_bank;
                    ur_r3_b7.para_raddr[3].addr_word = acc_addr;
                    ur_r3_b7.para_r_smc_id_en[3] = kSmcIdEn;
                    ur_r3_timeline.push_back(
                        {t_ur_r3_bank7, serialize_code_msb(extract_ur_read_port_code17(ur_r3_b7, 3), 17)});

                    ev_rtovr14.push_back({t_vin0_bank2, serialize_code_msb(rtovr14_ur1_to_vin0, 7)});
                    ev_rtovr15.push_back({t_vin1_scale, serialize_code_msb(rtovr15_ur2_to_vin1, 7)});
                    ev_rtovr15.push_back({t_vin1_bank7, serialize_code_msb(rtovr15_ur3_to_vin1, 7)});
                    ev_vin0.push_back({t_vin0_bank2, vin_cru_bytes});
                    ev_vin1.push_back({t_vin1_scale, vin_cru_bytes});
                    ev_vin1.push_back({t_vin1_bank7, vin_cru_bytes});
                    ev_rtovr3_acc.push_back({t_route_mul, serialize_code_msb(rtovr3_vin0_to_fpmul_s0, 7)});
                    ev_rtovr7_acc.push_back({t_route_sc2fp, serialize_code_msb(rtovr7_vin1_to_fptofp, 7)});
                    ev_fptofp.push_back({t_fptofp, fptofp_code});
                    ev_rtovr4_acc.push_back({t_route_mul, serialize_code_msb(rtovr4_fptofp_to_fpmul_s1, 7)});
                    ev_rtovr5.push_back({t_route_add, serialize_code_msb(rtovr5_vin1_to_fpadd_s0, 7)});
                    ev_rtovr6.push_back({t_route_add, serialize_code_msb(rtovr6_fpmul_d_to_fpadd_s1, 7)});
                    ev_rtovr16.push_back({t_vout, serialize_code_msb(rtovr16_fpadd_d_to_vout0, 7)});
                    ev_vout0.push_back({t_vout, vin_cru_bytes});
                    ev_rtovr35.push_back({t_rtovr35_b, serialize_code_msb(rtovr35_vout_to_wdata0, 7)});
                    ev_fpmul_acc.push_back({t_fpmul, fp32_fpmul_code});
                    ev_fpadd.push_back({t_fpadd, fpadd_active_code});
                    wb_events.push_back({t_ur_w_b, acc_addr, bank7_out_bank});
                } else {
                    // fp16：bank2/bank7 经 VIN0/VIN1 进 fpadd，写回经 VOUT。
                    const uint16_t t_ur_r1_bank2 = t_read;
                    const uint16_t t_ur_r3_bank7 = t_read;
                    const uint16_t t_vin0 = static_cast<uint16_t>(t_read + kUrReadDataLatency);
                    const uint16_t t_vin1 = t_vin0;
                    const uint16_t t_route_add = static_cast<uint16_t>(t_vin0 + kVinLat);
                    const uint16_t t_fpadd = static_cast<uint16_t>(t_route_add + 2u);
                    const uint16_t t_fpadd_d = static_cast<uint16_t>(t_fpadd + kAccFpaddLat);
                    const uint16_t t_vout = t_fpadd_d;
                    const uint16_t t_rtovr35_b = static_cast<uint16_t>(t_vout + kVinLat);
                    const uint16_t t_ur_w_b = static_cast<uint16_t>(t_rtovr35_b + kAccUrWGap);

                    iq::UrCru ur_r1_b2{};
                    ur_r1_b2.para_vld = true;
                    ur_r1_b2.para_re[1] = true;
                    ur_r1_b2.para_raddr[1].bank = p.para_out_bank;
                    ur_r1_b2.para_raddr[1].addr_word = bank2_addr;
                    ur_r1_b2.para_r_smc_id_en[1] = kSmcIdEn;
                    ur_r1_timeline.push_back(
                        {t_ur_r1_bank2, serialize_code_msb(extract_ur_read_port_code17(ur_r1_b2, 1), 17)});

                    iq::UrCru ur_r3_b7{};
                    ur_r3_b7.para_vld = true;
                    ur_r3_b7.para_re[3] = true;
                    ur_r3_b7.para_raddr[3].bank = bank7_out_bank;
                    ur_r3_b7.para_raddr[3].addr_word = acc_addr;
                    ur_r3_b7.para_r_smc_id_en[3] = kSmcIdEn;
                    ur_r3_timeline.push_back(
                        {t_ur_r3_bank7, serialize_code_msb(extract_ur_read_port_code17(ur_r3_b7, 3), 17)});

                    ev_rtovr14.push_back({t_vin0, serialize_code_msb(rtovr14_ur1_to_vin0, 7)});
                    ev_rtovr15.push_back({t_vin1, serialize_code_msb(rtovr15_ur3_to_vin1, 7)});
                    ev_vin0.push_back({t_vin0, vin_cru_bytes});
                    ev_vin1.push_back({t_vin1, vin_cru_bytes});
                    ev_rtovr5.push_back({t_route_add, serialize_code_msb(rtovr5_vin1_to_fpadd_s0, 7)});
                    ev_rtovr6.push_back({t_route_add, serialize_code_msb(rtovr6_vin0_to_fpadd_s1, 7)});
                    ev_rtovr16.push_back({t_vout, serialize_code_msb(rtovr16_fpadd_d_to_vout0, 7)});
                    ev_vout0.push_back({t_vout, vin_cru_bytes});
                    ev_rtovr35.push_back({t_rtovr35_b, serialize_code_msb(rtovr35_vout_to_wdata0, 7)});
                    ev_fpadd.push_back({t_fpadd, fpadd_active_code});
                    wb_events.push_back({t_ur_w_b, acc_addr, bank7_out_bank});
                }
            }
        }

        if (use_gptq) {
            build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 3, "rtovr-3", 7, ev_rtovr3_acc,
                                           total_cycles, out->para_iq_files);
            build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 4, "rtovr-4", 7, ev_rtovr4_acc,
                                           total_cycles, out->para_iq_files);
            build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 7, "rtovr-7", 7, ev_rtovr7_acc,
                                           total_cycles, out->para_iq_files);
            build_iq_timeline(iq::IQ_ID_FP_TO_FP, iq::IQ_NAME_FP_TO_FP, 10, ev_fptofp,
                              total_cycles, out->para_iq_files);
            build_iq_timeline(iq::IQ_ID_FPMUL, iq::IQ_NAME_FPMUL, 8, ev_fpmul_acc, total_cycles,
                              out->para_iq_files);
        }
        build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 5, "rtovr-5", 7, ev_rtovr5,
                                       total_cycles, out->para_iq_files);
        build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 6, "rtovr-6", 7, ev_rtovr6,
                                       total_cycles, out->para_iq_files);
        build_iq_active_window_then_events(
            iq::IQ_ID_RTOVR_BASE + 14, "rtovr-14", 7, deq_vin_win_start, deq_vin_win_end,
            serialize_code_msb(rtovr14_zp_to_vin0, 7), ev_rtovr14, total_cycles,
            out->para_iq_files);
        build_iq_active_window_then_events(
            iq::IQ_ID_RTOVR_BASE + 15, "rtovr-15", 7, deq_vin_win_start, deq_vin_win_end,
            serialize_code_msb(rtovr15_q_to_vin1, 7), ev_rtovr15, total_cycles,
            out->para_iq_files);
        build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 16, "rtovr-16", 7, ev_rtovr16,
                                       total_cycles, out->para_iq_files);
        auto vin_window = [&](const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& ev)
            -> std::pair<uint16_t, uint16_t> {
            if (ev.empty()) return {0, 0};
            uint16_t t0 = ev.front().first;
            uint16_t t1 = ev.front().first;
            for (const auto& e : ev) {
                if (e.first < t0) t0 = e.first;
                if (e.first > t1) t1 = e.first;
            }
            // 覆盖 VIN/VOUT 锁存 + fpmul/fpadd 流水 + wdata 采样。
            t1 = static_cast<uint16_t>(t1 + kVinLat + kAccFpmulLat + kAccFpaddLat + kAccUrWGap + 4u);
            return {t0, t1};
        };
        {
            // 文件名必须带 IQ ID 前缀（iq_68_vin0.hex）：data_list 生成器靠
            // ^iq_<id>_<name>.hex 规则反推 IQ ID，裸 "vin0" 不在 IQ_NAME_TO_ID
            // 表里会被静默跳过，IQ 68/69/70 不加载 -> VIN/VOUT 使能不开 -> 全零
            // （与 sub/mul/pow 对齐，直接用字面量，勿用 IQ_NAME_VIN* 常量）。
            const auto w0 = vin_window(ev_vin0);
            build_iq_valid_hold_window(iq::IQ_ID_VIN0, "68_vin0", 7, vin_cru_bytes, w0.first,
                                       w0.second, total_cycles, out->para_iq_files);
            const auto w1 = vin_window(ev_vin1);
            build_iq_valid_hold_window(iq::IQ_ID_VIN1, "69_vin1", 7, vin_cru_bytes, w1.first,
                                       w1.second, total_cycles, out->para_iq_files);
            const auto wv = vin_window(ev_vout0);
            build_iq_valid_hold_window(iq::IQ_ID_VOUT0, "70_vout0", 7, vin_cru_bytes,
                                       wv.first, wv.second, total_cycles, out->para_iq_files);
        }
        build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 35, "rtovr-35", 7, ev_rtovr35,
                                       total_cycles, out->para_iq_files);
        build_iq_timeline(iq::IQ_ID_FPADD, iq::IQ_NAME_FPADD, 10, ev_fpadd, total_cycles,
                          out->para_iq_files);
        build_iq_timeline(iq::IQ_ID_UR_RDATA_1, iq::IQ_NAME_UR_RDATA_1, 17, ur_r1_timeline,
                          total_cycles, out->para_iq_files);
        // fp16 B 喂数已在上文用 loops+inc 直接 emit；此处仅 GPTQ（及 Phase B scale）走 timeline。
        if (!ur_r2_timeline.empty()) {
            build_iq_timeline(iq::IQ_ID_UR_RDATA_2, iq::IQ_NAME_UR_RDATA_2, 17, ur_r2_timeline,
                              total_cycles, out->para_iq_files);
        }
        build_iq_timeline(iq::IQ_ID_UR_RDATA_3, iq::IQ_NAME_UR_RDATA_3, 17, ur_r3_timeline,
                          total_cycles, out->para_iq_files);

        // 单口写回：Phase A(bank2) + Phase B(bank7) 事件按 fire_cycle 合并排序。
        build_ur_write_file_from_events(iq::IQ_ID_UR_WDATA_0, iq::IQ_NAME_UR_WDATA_0, 0, ur_w,
                                        total_cycles, wb_events, out->para_iq_files);
    }

    return out.release();
}

// ===== M=256 f16 epilogue（D4）：bank7(fp32) → bank6(packed f16) =====
// bank7：每 SMC 256 个 fp32 word = 256 行 × 4 列（lane i = 列 4s+(3−i)）
// bank6：每 SMC 128 个 packed f16 word，每 word = 行对 (2r,2r+1) × 4 列
//   （dst_pos=0 写偶半字 = 行 2r，dst_pos=1 写奇半字 = 行 2r+1 并保留偶半字）
// 一个 stage 处理 para_ep_words 个输出字（32 字 = 64 行 = 8 个 row-group，4 个 stage 覆盖
// 全部 128 word）。切分依据 INSTQ_DEPTH=128：所有 IQ 均逐事件显式展开（fptofp/rtovr-7
// 交替两码每字 2 事件 + 间隙 NOP ≈ 3 行/字，32 字 ≈ 99 行；单码流 ≈ 2 行/字）。
// 不用 loops+dly 压缩 stride：embed 嵌套循环路径会把第 2 次迭代起的 dly 清零
// （smi_iq_iic.v :717/:719），dly 只是首拍延迟而非发拍间隔（阶梯 1 v3 波形实证）。
//
// 逐字时序（T = kEpStartGap + r*kEpStride），ur-r3 读比 ur-r1 错开 1 拍（UR 每 bank
// 只有一条物理读口，同拍双读撞口；user_register.v hit_r0>r1>r2>r3 优先级 mux），
// 全部走 FPGA 稀疏掩码合法的组合（mask7=0x1FDB 含 bit11/12；mask14/15 含 bit30/33；
// mask16 含 bit6；mask35 含 bit13）：
//   UR_R1 读 bank7[2r]   @ T   ─→ rtovr-14(UR_RDATA_1) @ T+2 → VIN0 ─┐
//   UR_R3 读 bank7[2r+1] @ T+1 ─→ rtovr-15(UR_RDATA_3) @ T+3 → VIN1 ─┤
//   rtovr-7(VIN0_D) @ T+4 → fptofp(dst_pos=0) @ T+5                    │
//   rtovr-7(VIN1_D) @ T+5 → fptofp(dst_pos=1) @ T+6（读 output_data_r 偶半字）┘
//   fptofp_d @ T+7 → rtovr-16 → VOUT0（锁存 T+7）→ VOUT0_D @ T+9
//   → rtovr-35 @ T+9 → uvr_wdata0 → 写 bank6[dst+r] @ T+10
// 偏移全部沿用 Phase B 已验证的 UR→VIN(+2)→消费(+4)→运算(+5) 相对结构。
OpInstructionsBase* MatmulNtW4f16Op::compile_f16_epilogue(const app::AppOperatorRequest& req,
                                                          const MatmulNtW4f16Params& p) {
    (void)req;
    auto out = std::make_unique<MatmulNtW4f16Instructions>();
    const uint8_t kSmcIdEn = static_cast<uint8_t>(std::min<uint32_t>(31u, p.para_num_smc));
    const uint8_t vin_cru = static_cast<uint8_t>(((kSmcIdEn & 0x1Fu) << 1) | 1u);
    const auto vin_cru_bytes = serialize_code_msb(vin_cru, 7);

    constexpr uint16_t kEpStartGap = 8u;   // 与上一 stage 排空留余量
    constexpr uint16_t kEpStride = 4u;     // 每输出字 4 拍（读/转换/写全流水，互不重叠）
    constexpr uint16_t kEpWriteOff = 10u;  // T → UR 写使能
    constexpr uint16_t kEpTail = 8u;

    const uint16_t words = static_cast<uint16_t>(p.para_ep_words);
    const uint16_t total_cycles = static_cast<uint16_t>(
        kEpStartGap + static_cast<uint32_t>(words - 1u) * kEpStride + kEpWriteOff + 1u + kEpTail);
    out->para_total_steps = total_cycles;
    out->para_total_elements = static_cast<uint32_t>(words) * 8u;  // 每 word 8 个 f16

    // RtoVR code = (src_id << 1) | vld
    const auto rtovr14_ur1_to_vin0 = serialize_code_msb((31u << 1) | 1u, 7);  // UR_RDATA_1 → VIN0
    const auto rtovr15_ur3_to_vin1 = serialize_code_msb((33u << 1) | 1u, 7);  // UR_RDATA_3 → VIN1
    const auto rtovr7_vin0d = serialize_code_msb((11u << 1) | 1u, 7);         // VIN0_D → fptofp
    const auto rtovr7_vin1d = serialize_code_msb((12u << 1) | 1u, 7);         // VIN1_D → fptofp
    const auto rtovr16_fptofp_d = serialize_code_msb((6u << 1) | 1u, 7);      // fptofp_d → VOUT0
    const auto rtovr35_vout0d = serialize_code_msb((13u << 1) | 1u, 7);       // VOUT0_D → wdata0

    iq::FpToFpCru fptofp_cru{};
    fptofp_cru.para_inst_valid = true;
    fptofp_cru.para_src_is_32b = true;   // 源 fp32
    fptofp_cru.para_dst_is_32b = false;  // 目的 fp16
    fptofp_cru.para_src_high = false;
    fptofp_cru.para_smc_id_en = kSmcIdEn;
    fptofp_cru.para_dst_high = false;  // dst_pos=0：写偶半字（行 2r）
    const auto fptofp_dst_lo_code =
        serialize_code_msb(iq::FpToFpIq::pack_cru_fptofp(fptofp_cru), 10);
    fptofp_cru.para_dst_high = true;  // dst_pos=1：写奇半字（行 2r+1），保留 output_data_r 偶半字
    const auto fptofp_dst_hi_code =
        serialize_code_msb(iq::FpToFpIq::pack_cru_fptofp(fptofp_cru), 10);

    // ---- 流 1：UR_R1 / UR_R3 同拍读（行对 2r/2r+1，逐 word 一条显式地址指令）----
    // 注意：不用 IIC inc 自增——阶梯 1 实测 inc 路径读地址未按预期递增（地址恒定），
    // 根因未明前先用显式地址（每 word 1 行 + NOP 间隔，32 word 约 66 行 ≤ INSTQ_DEPTH=128）。
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rd1;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rd3;
    ev_rd1.reserve(words);
    ev_rd3.reserve(words);
    for (uint16_t r = 0; r < words; ++r) {
        const uint16_t t = static_cast<uint16_t>(kEpStartGap + static_cast<uint32_t>(r) * kEpStride);
        iq::UrCru rd1{};
        rd1.para_vld = true;
        rd1.para_re[1] = true;
        rd1.para_raddr[1].bank = p.para_ep_src_bank;
        rd1.para_raddr[1].addr_word = static_cast<uint8_t>(p.para_ep_src_addr + 2u * r);
        rd1.para_r_smc_id_en[1] = kSmcIdEn;
        ev_rd1.push_back({t, serialize_code_msb(extract_ur_read_port_code17(rd1, 1), 17)});
        // 关键：UR 每个 bank 只有一条读口（user_register.v hit_r0>r1>r2>r3 优先级mux），
        // 同拍同 bank 的双读会撞口——port3 的地址被忽略、rdata_3 拿到 port1 的数据
        // （阶梯 1 v3 实测 hi 行=lo 行复制的根因）。故 ur-r3 读错开 1 拍到 T+1。
        iq::UrCru rd3{};
        rd3.para_vld = true;
        rd3.para_re[3] = true;
        rd3.para_raddr[3].bank = p.para_ep_src_bank;
        rd3.para_raddr[3].addr_word = static_cast<uint8_t>(p.para_ep_src_addr + 2u * r + 1u);
        rd3.para_r_smc_id_en[3] = kSmcIdEn;
        ev_rd3.push_back({static_cast<uint16_t>(t + 1u),
                          serialize_code_msb(extract_ur_read_port_code17(rd3, 3), 17)});
    }
    build_iq_timeline_idle_invalid(iq::IQ_ID_UR_RDATA_1, iq::IQ_NAME_UR_RDATA_1, 17, ev_rd1,
                                   total_cycles, out->para_iq_files);
    build_iq_timeline_idle_invalid(iq::IQ_ID_UR_RDATA_3, iq::IQ_NAME_UR_RDATA_3, 17, ev_rd3,
                                   total_cycles, out->para_iq_files);

    // ---- 显式事件流 2：rtovr-14 / rtovr-15 / rtovr-16 / rtovr-35（T+2 / T+3 / T+7 / T+9）----
    // 不用 loops+dly 压缩：smi_iq_iic 的 embed 嵌套循环路径会把第 2 次起每次迭代的 dly
    // 清零（smi_iq_iic.v :717/:719 分支），loops=3,dly=3 实测变成 4 拍连发（阶梯 1 v3
    // dbg_sel_latch 波形证实 rtovr-14 在 cyc10..13 连续触发），stride>1 只能逐发展开。
    // rtovr-15 错到 T+3（ur-r3 读在 T+1，rdata 迟 1 拍；VIN1 窗 10..27 仍覆盖，
    // rtovr-7 hi @T+5 采到的 VIN1_D 正好是 T+3 路由进来的 w(2r+1)）。
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_r14;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_r15;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_r16;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_r35;
    ev_r14.reserve(words);
    ev_r15.reserve(words);
    ev_r16.reserve(words);
    ev_r35.reserve(words);
    for (uint16_t r = 0; r < words; ++r) {
        const uint16_t t = static_cast<uint16_t>(kEpStartGap + static_cast<uint32_t>(r) * kEpStride);
        ev_r14.push_back({static_cast<uint16_t>(t + 2u), rtovr14_ur1_to_vin0});
        ev_r15.push_back({static_cast<uint16_t>(t + 3u), rtovr15_ur3_to_vin1});
        ev_r16.push_back({static_cast<uint16_t>(t + 7u), rtovr16_fptofp_d});
        ev_r35.push_back({static_cast<uint16_t>(t + 9u), rtovr35_vout0d});
    }
    build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 14, "rtovr-14", 7, ev_r14,
                                   total_cycles, out->para_iq_files);
    build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 15, "rtovr-15", 7, ev_r15,
                                   total_cycles, out->para_iq_files);
    build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 16, "rtovr-16", 7, ev_r16,
                                   total_cycles, out->para_iq_files);
    build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 35, "rtovr-35", 7, ev_r35,
                                   total_cycles, out->para_iq_files);

    // ---- 交替两码：rtovr-7（VIN0_D/VIN1_D @ T+4/T+5）与 fptofp（dst_lo/hi @ T+5/T+6）----
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_rtovr7;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> ev_fptofp;
    ev_rtovr7.reserve(2u * words);
    ev_fptofp.reserve(2u * words);
    for (uint16_t r = 0; r < words; ++r) {
        const uint16_t t = static_cast<uint16_t>(kEpStartGap + static_cast<uint32_t>(r) * kEpStride);
        ev_rtovr7.push_back({static_cast<uint16_t>(t + 4u), rtovr7_vin0d});
        ev_rtovr7.push_back({static_cast<uint16_t>(t + 5u), rtovr7_vin1d});
        ev_fptofp.push_back({static_cast<uint16_t>(t + 5u), fptofp_dst_lo_code});
        ev_fptofp.push_back({static_cast<uint16_t>(t + 6u), fptofp_dst_hi_code});
    }
    build_iq_timeline_idle_invalid(iq::IQ_ID_RTOVR_BASE + 7, "rtovr-7", 7, ev_rtovr7, total_cycles,
                                   out->para_iq_files);
    build_iq_timeline(iq::IQ_ID_FP_TO_FP, iq::IQ_NAME_FP_TO_FP, 10, ev_fptofp, total_cycles,
                      out->para_iq_files);

    // ---- VIN0/VIN1/VOUT0 保持窗口（smi_top 在 vld=0 时清 CRU，必须整窗保持）----
    const uint16_t t_last_word = static_cast<uint16_t>(
        kEpStartGap + static_cast<uint32_t>(words - 1u) * kEpStride);
    // 文件名必须带 IQ ID 前缀（iq_68_vin0.hex），data_list 生成器靠该规则反推 IQ ID。
    build_iq_valid_hold_window(iq::IQ_ID_VIN0, "68_vin0", 7, vin_cru_bytes,
                               static_cast<uint16_t>(kEpStartGap + 2u),
                               static_cast<uint16_t>(t_last_word + 6u + 2u), total_cycles,
                               out->para_iq_files);
    build_iq_valid_hold_window(iq::IQ_ID_VIN1, "69_vin1", 7, vin_cru_bytes,
                               static_cast<uint16_t>(kEpStartGap + 2u),
                               static_cast<uint16_t>(t_last_word + 6u + 2u), total_cycles,
                               out->para_iq_files);
    build_iq_valid_hold_window(iq::IQ_ID_VOUT0, "70_vout0", 7, vin_cru_bytes,
                               static_cast<uint16_t>(kEpStartGap + 7u),
                               static_cast<uint16_t>(t_last_word + 10u + 2u), total_cycles,
                               out->para_iq_files);

    // ---- 显式事件流 3：UR_W0 写 bank6（T+10 起 stride 4，addr = dst + r 逐条显式）----
    // 同 rtovr 流：IIC loops+dly 的 stride>1 连发不可用（dly 被 embed 路径清零），
    // 且 inc 自增此前实测异常（阶梯 1 v2 地址不递增），改用 Phase B 已验证的显式逐条写。
    iq::UrCru ur_w_ep{};
    ur_w_ep.para_vld = true;
    ur_w_ep.para_we[0] = true;
    ur_w_ep.para_wbe[0] = 0xFFFFu;
    ur_w_ep.para_waddr[0].bank = p.para_ep_dst_bank;
    ur_w_ep.para_waddr[0].addr_word = p.para_ep_dst_addr;
    ur_w_ep.para_w_smc_id_en[0] = kSmcIdEn;
    std::vector<UrWbEvent> ev_wr0;
    ev_wr0.reserve(words);
    for (uint16_t r = 0; r < words; ++r) {
        UrWbEvent ev{};
        ev.fire_cycle = static_cast<uint16_t>(kEpStartGap + kEpWriteOff +
                                              static_cast<uint32_t>(r) * kEpStride);
        ev.bank = p.para_ep_dst_bank;
        ev.addr_word = static_cast<uint8_t>(p.para_ep_dst_addr + r);
        ev_wr0.push_back(ev);
    }
    build_ur_write_file_from_events(iq::IQ_ID_UR_WDATA_0, iq::IQ_NAME_UR_WDATA_0, 0, ur_w_ep,
                                    total_cycles, ev_wr0, out->para_iq_files);
    return out.release();
}

bool MatmulNtW4f16Op::calculate_golden(const app::AppOperatorRequest& req,
                              std::vector<uint8_t>& golden_bin) {
    golden_bin.clear();
    const auto a_it = req.para_inputs.find("a_tensor");
    const auto b_it = req.para_inputs.find("b_tensor");
    if (a_it == req.para_inputs.end() || b_it == req.para_inputs.end()) return false;
    const auto& a = a_it->second.para_data_f32;
    const auto& b_f32 = b_it->second.para_data_f32;
    const auto& b_i32 = b_it->second.para_data_i32;
    constexpr size_t kDim = 64u;
    if (a.size() < kDim * kDim) return false;
    if (b_f32.size() < kDim * kDim && b_i32.empty()) return false;
    std::vector<float> b_as_f32(kDim * kDim, 0.0f);
    if (!b_i32.empty()) {
        if (b_i32.size() == 1u) {
            const float v = static_cast<float>(clamp_u4_from_i32(b_i32[0]));
            std::fill(b_as_f32.begin(), b_as_f32.end(), v);
        } else {
            for (size_t i = 0; i < b_as_f32.size() && i < b_i32.size(); ++i) {
                b_as_f32[i] = static_cast<float>(clamp_u4_from_i32(b_i32[i]));
            }
        }
    } else {
        for (size_t i = 0; i < b_as_f32.size() && i < b_f32.size(); ++i) {
            b_as_f32[i] = b_f32[i];
        }
    }

    const MatmulNtW4f16Params p = parse_request(req);
    std::vector<float> c(kDim * kDim, 0.0f);
    const size_t k_total = static_cast<size_t>(p.para_K);
    for (size_t i = 0; i < kDim; ++i) {
        for (size_t j = 0; j < kDim; ++j) {
            double acc = 0.0;
            for (size_t k = 0; k < k_total; ++k) {
                const float af = app::float16_to_float32(
                    app::float32_to_float16(a[i * kDim + k]));
                const float bf = app::float16_to_float32(
                    app::float32_to_float16(b_as_f32[k * kDim + j]));
                acc += static_cast<double>(af) * static_cast<double>(bf);
            }
            c[i * kDim + j] = static_cast<float>(acc);
        }
    }

    // 与输入数据口径统一：单/多 SMC 均按“转置+间插=2”写 golden。
    pack_c_transpose_interleave2_8x8(c, golden_bin);
    return true;
}

bool MatmulNtW4f16Op::generate_data(const app::AppOperatorRequest& req,
                           std::vector<uint8_t>& input_bin,
                           std::vector<uint8_t>& other_bin,
                           std::vector<uint8_t>& alpha_bin) {
    input_bin.clear();
    other_bin.clear();
    alpha_bin.clear();
    const MatmulNtW4f16Params p = parse_request(req);
    const auto a_it = req.para_inputs.find("a_tensor");
    const auto b_it = req.para_inputs.find("b_tensor");
    if (a_it == req.para_inputs.end() || b_it == req.para_inputs.end()) return false;
    const auto& a = a_it->second.para_data_f32;
    const auto& b_f32 = b_it->second.para_data_f32;
    const auto& b_i32 = b_it->second.para_data_i32;
    const auto zp_it = req.para_inputs.find("zp_tensor");
    constexpr size_t kDim = 64u;
    if (a.size() < kDim * kDim) return false;
    if (b_i32.empty() && b_f32.size() < kDim * kDim) return false;
    std::vector<uint8_t> bq_u4(kDim * kDim, 0u);
    if (!b_i32.empty()) {
        if (b_i32.size() == 1u) {
            std::fill(bq_u4.begin(), bq_u4.end(), clamp_u4_from_i32(b_i32[0]));
        } else {
            for (size_t i = 0; i < bq_u4.size() && i < b_i32.size(); ++i) {
                bq_u4[i] = clamp_u4_from_i32(b_i32[i]);
            }
        }
    } else {
        for (size_t i = 0; i < bq_u4.size() && i < b_f32.size(); ++i) {
            bq_u4[i] = extract_u4_from_fp16_bits(b_f32[i]);
        }
    }

    // input.bin：保留整包 A（tvgen ur_00.hex / 单 SMC 调试兼容）
    pack_a_transpose_interleave2_8x8(a, input_bin);

    // other.bin：MSH5 多 SMC 分块（adapt 写入 smc_0..7 各自 UR）
    // 首阶段固定 tile：A(8x32) 总计 32 words；
    // B_q 改为 int4 packed：每 SMC 32x4=128 个 int4 => 64B => 4 words。
    constexpr uint32_t kAWordsPerSmc = 4u;
    constexpr uint32_t kBWordsPerSmc = 4u;
    std::vector<std::vector<uint8_t>> smc_a;
    std::vector<std::vector<uint8_t>> smc_b;
    pack_a_per_smc_transpose_interleave2(a, p.para_num_smc, kAWordsPerSmc, smc_a);
    pack_bq_u4_per_smc(bq_u4, p.para_num_smc, smc_b);
    if (smc_a.size() < p.para_num_smc || smc_b.size() < p.para_num_smc) return false;

    // 方案A：离线将 zp(uint4) 展开为 int8，并作为 optional trailer 写入 other.bin，
    // 供 adapt_mm_case.sh 落到 bank4。
    constexpr uint32_t kGroupSize = 64u;        // 用户确认：group_size=64
    constexpr uint32_t kZpWordsPerSmc = 1u;     // 每个 SMC 先放 1 个 128b 模板 word（16xint8）
    const uint8_t zp_bank = get_i32_param<uint8_t>(req, "zp_bank", 4);
    const uint8_t zp_addr = get_i32_param<uint8_t>(req, "zp_addr", 0);
    const uint8_t scale_bank = get_i32_param<uint8_t>(req, "scale_bank", 5);
    const uint8_t scale_addr = get_i32_param<uint8_t>(req, "scale_addr", 0);
    std::vector<uint8_t> zp_col_u4(kDim, 0u);   // 每列一个 zp（K=32 阶段下 group_size=64 等价每列单组）
    std::vector<float> scale_col(kDim, 1.0f);   // 每列一个 scale（group_size=64）
    if (zp_it != req.para_inputs.end()) {
        if (!zp_it->second.para_data_i32.empty()) {
            const auto& vi = zp_it->second.para_data_i32;
            if (vi.size() == 1u) {
                std::fill(zp_col_u4.begin(), zp_col_u4.end(), clamp_u4_from_i32(vi[0]));
            } else {
                for (size_t c = 0; c < kDim && c < vi.size(); ++c) {
                    zp_col_u4[c] = clamp_u4_from_i32(vi[c]);
                }
            }
        } else if (!zp_it->second.para_data_f32.empty()) {
            const auto& vf = zp_it->second.para_data_f32;
            if (vf.size() == 1u) {
                std::fill(zp_col_u4.begin(), zp_col_u4.end(), clamp_u4_from_f32(vf[0]));
            } else {
                for (size_t c = 0; c < kDim && c < vf.size(); ++c) {
                    zp_col_u4[c] = clamp_u4_from_f32(vf[c]);
                }
            }
        }
    }
    const auto sc_it = req.para_inputs.find("scale_tensor");
    if (sc_it != req.para_inputs.end()) {
        if (!sc_it->second.para_data_f32.empty()) {
            const auto& vf = sc_it->second.para_data_f32;
            if (vf.size() == 1u) {
                std::fill(scale_col.begin(), scale_col.end(), clamp_scale_from_f32(vf[0]));
            } else {
                for (size_t c = 0; c < kDim && c < vf.size(); ++c) {
                    scale_col[c] = clamp_scale_from_f32(vf[c]);
                }
            }
        } else if (!sc_it->second.para_data_i32.empty()) {
            const auto& vi = sc_it->second.para_data_i32;
            if (vi.size() == 1u) {
                std::fill(scale_col.begin(), scale_col.end(),
                          clamp_scale_from_f32(static_cast<float>(vi[0])));
            } else {
                for (size_t c = 0; c < kDim && c < vi.size(); ++c) {
                    scale_col[c] = clamp_scale_from_f32(static_cast<float>(vi[c]));
                }
            }
        }
    }
    std::vector<std::vector<uint8_t>> smc_zp(p.para_num_smc);
    std::vector<std::vector<uint8_t>> smc_scale(p.para_num_smc);
    for (uint32_t s = 0; s < p.para_num_smc; ++s) {
        // 与 B_q 保持同列映射：s, s+8, s+16, s+24（0-based）。
        std::vector<uint8_t> payload;
        payload.reserve(kZpWordsPerSmc * 16u);
        // 16 lanes: c0,c1,c2,c3 重复 4 次，便于后续 intadd 8-bit 基底向量直接复用。
        for (uint32_t rep = 0; rep < 4u; ++rep) {
            for (uint32_t n = 0; n < 4u; ++n) {
                const uint32_t col = s + 8u * n;
                const uint8_t zp = (col < kDim) ? zp_col_u4[col] : 0u;
                payload.push_back(zp);  // int8-expanded (unsigned 0..15)
            }
        }
        smc_zp[s] = std::move(payload);

        // scale 模板：1 个 128b word，按 [c0,c1,c2,c3] 的 fp32 展开，供写回段 fpmul 使用。
        std::vector<uint8_t> scale_payload;
        scale_payload.reserve(16u);
        for (uint32_t n = 0; n < 4u; ++n) {
            const uint32_t col = s + 8u * n;
            const float sc = (col < kDim) ? scale_col[col] : 1.0f;
            append_fp32_le(scale_payload, sc);
        }
        smc_scale[s] = std::move(scale_payload);
    }

    append_u32_le(other_bin, kMagicMsh5);
    append_u32_le(other_bin, p.para_num_smc);
    append_u32_le(other_bin, p.para_a_bank);
    append_u32_le(other_bin, p.para_a_addr);
    append_u32_le(other_bin, p.para_b_bank);
    append_u32_le(other_bin, p.para_b_addr);
    append_u32_le(other_bin, static_cast<uint32_t>(kDim));  // M
    append_u32_le(other_bin, static_cast<uint32_t>(kDim));  // W
    append_u32_le(other_bin, kAWordsPerSmc);
    append_u32_le(other_bin, kBWordsPerSmc);
    for (uint32_t s = 0; s < p.para_num_smc; ++s) {
        append_u32_le(other_bin, static_cast<uint32_t>(smc_a[s].size()));
        other_bin.insert(other_bin.end(), smc_a[s].begin(), smc_a[s].end());
        append_u32_le(other_bin, static_cast<uint32_t>(smc_b[s].size()));
        other_bin.insert(other_bin.end(), smc_b[s].begin(), smc_b[s].end());
    }
    // optional ZP trailer（保持旧 MSH5 头兼容）
    append_u32_le(other_bin, kMagicZp8);
    append_u32_le(other_bin, p.para_num_smc);
    append_u32_le(other_bin, static_cast<uint32_t>(zp_bank));
    append_u32_le(other_bin, static_cast<uint32_t>(zp_addr));
    append_u32_le(other_bin, kGroupSize);
    append_u32_le(other_bin, kZpWordsPerSmc);
    for (uint32_t s = 0; s < p.para_num_smc; ++s) {
        append_u32_le(other_bin, static_cast<uint32_t>(smc_zp[s].size()));
        other_bin.insert(other_bin.end(), smc_zp[s].begin(), smc_zp[s].end());
    }
    // optional SCALE trailer（fp16 模板）
    append_u32_le(other_bin, kMagicSc16);
    append_u32_le(other_bin, p.para_num_smc);
    append_u32_le(other_bin, static_cast<uint32_t>(scale_bank));
    append_u32_le(other_bin, static_cast<uint32_t>(scale_addr));
    append_u32_le(other_bin, kGroupSize);
    append_u32_le(other_bin, 1u);  // words_per_smc
    for (uint32_t s = 0; s < p.para_num_smc; ++s) {
        append_u32_le(other_bin, static_cast<uint32_t>(smc_scale[s].size()));
        other_bin.insert(other_bin.end(), smc_scale[s].begin(), smc_scale[s].end());
    }
    return true;
}

void MatmulNtW4f16Op::fill_random_request(app::AppOperatorRequest& req,
                                 std::mt19937& rng,
                                 std::uniform_real_distribution<float>& data_dist,
                                 const std::string&) {
    // 默认 M=8：A 8×K；MM_M=1 时生成 decode GEMV：A 1×K（K 为 256 倍数）。
    // MM_M=256：A 256×K（随机兜底路径；正式跑用 MM_TENSOR_DIR 外部张量）。
    uint32_t mRows = 8u;
    if (const char* e = std::getenv("MM_M")) {
        const int vv = std::atoi(e);
        if (vv == 1 || vv == 8 || vv == static_cast<int>(kMatmulNtW4f16M256))
            mRows = static_cast<uint32_t>(vv);
    }
    uint32_t kTot = (mRows == 1u) ? kMatmulNtW4f16M1KTile : 64u;
    if (const char* e = std::getenv("K_TOTAL")) {
        const int vv = std::atoi(e);
        if (mRows == 1u) {
            if (vv >= static_cast<int>(kMatmulNtW4f16M1KTile) && (vv % static_cast<int>(kMatmulNtW4f16M1KTile)) == 0)
                kTot = static_cast<uint32_t>(vv);
        } else if (vv >= 32 && (vv % 32) == 0) {
            kTot = static_cast<uint32_t>(vv);
        }
    }
    constexpr uint32_t kNCols = 64u;

    req.para_operator = "MatMul_NT_w4f16";
    req.para_dimensions.para_n = 1;
    req.para_dimensions.para_c = 1;
    req.para_dimensions.para_h = static_cast<int>(mRows);
    req.para_dimensions.para_w = static_cast<int>(kNCols);

    app::AppTensorDesc a, b, zp, scale;
    a.para_name = "a_tensor";
    b.para_name = "b_tensor";
    a.para_dtype = app::AppDataType::FLOAT16;
    const bool want_fp16 = false;  // MatMul_NT_w4f16 固定 GPTQ（与 parse_request/LSU 同口径，不读 MM_QUANT）。
    b.para_dtype = want_fp16 ? app::AppDataType::FLOAT16 : app::AppDataType::INT4;
    a.para_shape = {1u, 1u, mRows, kTot};
    b.para_shape = {1u, 1u, kTot, kNCols};

    a.para_data_f32.resize(static_cast<size_t>(mRows) * kTot);
    for (float& v : a.para_data_f32) {
        v = data_dist(rng);
    }

    if (want_fp16) {
        b.para_data_f32.resize(static_cast<size_t>(kTot) * kNCols);
        for (float& v : b.para_data_f32) {
            v = data_dist(rng);
        }
    } else {
        b.para_data_i32.resize(static_cast<size_t>(kTot) * kNCols);
        std::uniform_int_distribution<int32_t> u4_dist(0, 15);
        for (int32_t& v : b.para_data_i32) {
            v = u4_dist(rng);
        }
    }

    zp.para_name = "zp_tensor";
    scale.para_name = "scale_tensor";
    zp.para_dtype = app::AppDataType::INT4;
    scale.para_dtype = app::AppDataType::FLOAT16;
    zp.para_shape = {1, 1, 1, 64};
    scale.para_shape = {1, 1, 1, 64};
    std::uniform_int_distribution<int32_t> u4_dist(0, 15);
    zp.para_data_i32.resize(64);
    for (int32_t& v : zp.para_data_i32) {
        v = u4_dist(rng);
    }
    scale.para_data_f32.resize(64);
    std::uniform_real_distribution<float> scale_dist(0.125f, 4.0f);
    for (float& v : scale.para_data_f32) {
        v = scale_dist(rng);
    }
    req.para_inputs["a_tensor"] = a;
    req.para_inputs["b_tensor"] = b;
    if (!want_fp16) {
        req.para_inputs["zp_tensor"] = zp;
        req.para_inputs["scale_tensor"] = scale;
    }

    auto put_i32 = [&](const std::string& key, int v) {
        if (req.para_parameters.find(key) != req.para_parameters.end()) return;
        app::AppTensorDesc d;
        d.para_data_i32.push_back(v);
        req.para_parameters[key] = d;
    };
    put_i32("a_bank", 0);
    put_i32("b_bank", 1);
    put_i32("out_bank", 2);
    put_i32("a_addr", 0);
    put_i32("b_addr", 0);
    put_i32("out_addr", 0);
    put_i32("zp_bank", 4);
    put_i32("zp_addr", 0);
    put_i32("M", static_cast<int>(mRows));
    if (mRows == 1u) put_i32("K_tile", static_cast<int>(kMatmulNtW4f16M1KTile));
    put_i32("scale_bank", 5);
    put_i32("scale_addr", 0);
    put_i32("K", static_cast<int>(kTot));
    put_i32("input_bank", 0);
    put_i32("other_bank", 1);
    put_i32("output_bank", 2);
    put_i32("input_addr", 0);
    put_i32("other_addr", 0);
    put_i32("output_addr", 0);
    put_i32("num_smc", 8);
}

void MatmulNtW4f16Op::pack_b_fp16_dense_interleave_kblock(const std::vector<uint16_t>& b_fp16,
                                               uint32_t num_smc,
                                               uint32_t words_per_smc,
                                               uint32_t k_block,
                                               std::vector<std::vector<uint8_t>>& smc_banks,
                                               uint32_t n_stride,
                                               uint32_t col_base) {
    std::vector<uint8_t> full;
    matmul_nt_w4f16_pack_b_fp16_transpose_dense_kchunk(b_fp16, full, k_block, n_stride, col_base);
    permute_words_contiguous_smc_cols(full);  // 4 层 × 32 word，逐层置换
    distribute_2word_interleave(full, num_smc, words_per_smc, smc_banks);
}

REGISTER_OP(MatmulNtW4f16Op);

}  // namespace op
}  // namespace aiacc

