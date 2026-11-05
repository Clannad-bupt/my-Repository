#ifndef MATMUL_NT_W4F16_OP_H
#define MATMUL_NT_W4F16_OP_H

#include <random>
#include <string>
#include <vector>

#include "../../app/app_json_parser.h"
#include "../common/op_base.h"
#include "../common/op_instructions_base.h"
#include "../common/op_registry.h"

namespace aiacc {
namespace op {

struct MatmulNtW4f16Instructions : public OpInstructionsBase {
    std::vector<IqInstructionFile> para_iq_files;
    uint32_t para_total_steps{1};
    uint32_t para_total_elements{1};

    std::vector<IqInstructionFile> get_iq_files() const override { return para_iq_files; }
    std::string get_operator_name() const override { return "MatMul_NT_w4f16"; }
    uint32_t get_total_steps() const override { return para_total_steps; }
    uint32_t get_total_elements() const override { return para_total_elements; }
    std::vector<uint8_t> serialize() const override { return {}; }
};

struct MatmulNtW4f16Params {
    uint8_t para_a_bank{0};
    uint8_t para_b_bank{1};
    uint8_t para_out_bank{2};
    uint8_t para_a_addr{0};
    uint8_t para_b_addr{0};
    uint8_t para_out_addr{0};
    uint32_t para_K{64};
    uint32_t para_num_smc{8};
    /** 1 = decode GEMV（1×K_tile）；8 = 现网 GEMM tile */
    uint32_t para_M{8};
    /** M=1 外层 K 块，契约为 256；M=8 微内核仍为 32 */
    uint32_t para_K_tile{32};
    /** MatMul_NT_w4f16：固定 GPTQ INT4（parse_request 恒置 true，不读任何参数/环境变量）。 */
    bool para_quantized{true};
    /** N 维：总列数（外部 B/zp/scale 文件宽度）与本 case 的 N-tile 序号（每 tile 32 列）。 */
    uint32_t para_N_total{32};
    uint32_t para_n_tile_idx{0};
    /** M=256 f16 epilogue（D4）：本 stage 的 bank6 输出字基址/字数 + bank7 源行基址。
     *  para_ep_words > 0 时 compile() 走 compile_f16_epilogue（其余参数不参与）。 */
    uint8_t para_ep_src_addr{0};
    uint8_t para_ep_dst_addr{0};
    uint32_t para_ep_words{0};
    uint8_t para_ep_src_bank{7};
    uint8_t para_ep_dst_bank{6};
};

/** M=1 契约常量（见 MM_M1_PLAN.md） */
constexpr uint32_t kMatmulNtW4f16M1KTile = 256u;
constexpr uint32_t kMatmulNtW4f16M1NTile = 32u;
constexpr uint32_t kMatmulNtW4f16M8KChunk = 32u;
/** M=256 prefill：一个 compute stage 内循环 32 个 row-group（每组 8 行复用 M=8 微内核）。 */
constexpr uint32_t kMatmulNtW4f16M256 = 256u;
constexpr uint32_t kMatmulNtW4f16M256RowsPerGroup = 8u;
constexpr uint32_t kMatmulNtW4f16M256Groups =
    kMatmulNtW4f16M256 / kMatmulNtW4f16M256RowsPerGroup;  // 32

// S5 统一算子入口（A 走链路 + B 本地 + MAC）。
// 由单一 compile 统一产出 S4 + S3 的 IQ 时间轴。
class MatmulNtW4f16Op : public OpBase {
public:
    std::string get_name() const override;
    OpInstructionsBase* compile(const app::AppOperatorRequest& req) override;
    bool calculate_golden(const app::AppOperatorRequest& req,
                          std::vector<uint8_t>& golden_bin) override;
    bool generate_data(const app::AppOperatorRequest& req,
                       std::vector<uint8_t>& input_bin,
                       std::vector<uint8_t>& other_bin,
                       std::vector<uint8_t>& alpha_bin) override;
    void fill_random_request(app::AppOperatorRequest& req,
                             std::mt19937& rng,
                             std::uniform_real_distribution<float>& data_dist,
                             const std::string& dtype_str) override;

    // LSU LDB：按 K 块切片打包（k_block=0 → K[0:32]，k_block=1 → K[32:64]）
    static void pack_a_per_smc_kblock(const std::vector<float>& a,
                                      uint32_t num_smc,
                                      uint32_t words_per_smc,
                                      uint32_t k_block,
                                      std::vector<std::vector<uint8_t>>& smc_banks);
    static void pack_bq_u4_per_smc_kblock(const std::vector<uint8_t>& bq_u4,
                                          uint32_t num_smc,
                                          uint32_t k_block,
                                          std::vector<std::vector<uint8_t>>& smc_banks);

    // 新布局（写 DDR/HBM）：A→Aᵀ / B→Bᵀ 行主序密铺 + 2-word 块轮流分发到各 SMC。
    // N-tile 列窗：B 张量文件宽 n_stride 列，本 case 只打包全局列 [col_base, col_base+32)
    // （col_base = n_tile_idx*32，8 SMC × 连续 4 列）。
    static void pack_a_dense_interleave_kblock(const std::vector<float>& a,
                                               uint32_t num_smc,
                                               uint32_t words_per_smc,
                                               uint32_t k_block,
                                               std::vector<std::vector<uint8_t>>& smc_banks);
    static void pack_bq_dense_interleave_kblock(const std::vector<uint8_t>& bq_u4,
                                                uint32_t num_smc,
                                                uint32_t words_per_smc,
                                                uint32_t k_block,
                                                std::vector<std::vector<uint8_t>>& smc_banks,
                                                uint32_t n_stride = 64u,
                                                uint32_t col_base = 0u);

    // ---- M=256 prefill：32 个 row-group 的 Aᵀ(32×8) 块顺序拼接后再 2-word 间插。
    // 组 g（行 [8g,8g+8)）落在每 SMC 本地 word [4g,4g+4)，与 M=8 组内布局同构，
    // compile_core 组循环只需把 A 基址偏移 g*4 即可复用同一 ds_t/bank3 机制。
    static void pack_a_m256_dense_interleave_kblock(const std::vector<float>& a,
                                                    uint32_t num_smc,
                                                    uint32_t words_per_smc,
                                                    uint32_t k_block,
                                                    std::vector<std::vector<uint8_t>>& smc_banks);

    // ---- M=1（A 1×256 reshape→假 8×32 / B 8×K32 叠放）----
    // HBM：与 M=8 相同的转置密铺 + 2-word 间插；A 4 word/SMC，B 32 word/SMC（8 段×4）。
    // compute：ds→bank3 回放；第 r 轮乘 B 段 r；Phase B 累加 8 行→1 word。
    static void pack_a_m1_dense_interleave_kblock(const std::vector<float>& a,
                                                  uint32_t num_smc,
                                                  uint32_t words_per_smc,
                                                  uint32_t k_block,
                                                  std::vector<std::vector<uint8_t>>& smc_banks,
                                                  bool replicate = false);
    static void pack_bq_m1_dense_interleave_kblock(const std::vector<uint8_t>& bq_u4,
                                                   uint32_t num_smc,
                                                   uint32_t words_per_smc,
                                                   uint32_t k_block,
                                                   std::vector<std::vector<uint8_t>>& smc_banks,
                                                   uint32_t n_stride = 64u,
                                                   uint32_t col_base = 0u);

    /** M=1：单 K_tile=256 的 1 word（4×fp32 = 该 SMC 4 列 ×scale，供 bank7 累加）。
     *  zp_col/scale_col 可为单组(64)或多组平铺(num_groups×64)；group_size=0 表示整 tile 共用单组。 */
    static bool compute_bank2_golden_m1_ktile(uint8_t smc_id,
                                              const std::vector<float>& a,
                                              const std::vector<uint8_t>& bq_u4,
                                              const std::vector<uint8_t>& zp_col,
                                              const std::vector<float>& scale_col,
                                              uint32_t k_block,
                                              std::vector<uint8_t>& bank2_out);
    static bool compute_bank2_golden_m1_ktile(uint8_t smc_id,
                                              const std::vector<float>& a,
                                              const std::vector<uint8_t>& bq_u4,
                                              const std::vector<uint8_t>& zp_all,
                                              const std::vector<float>& scale_all,
                                              uint32_t k_block,
                                              uint32_t group_size,
                                              std::vector<uint8_t>& bank2_out,
                                              uint32_t n_stride = 64u,
                                              uint32_t col_base = 0u);

    // zp/scale 打包（路线A）：per-列复制成整字（zp:u8×16 / scale:fp16×8）+ 与 B 相同的
    // 2-word 间插分发到各 SMC。供 LSU 写 HBM，LDB 原样搬进 bank4(zp)/bank5(scale)。
    // col_base：zp_col/scale_col 为全局 N 宽向量，本 case 取 [col_base, col_base+32)。
    static void pack_zp_dense_interleave(const std::vector<uint8_t>& zp_col,
                                         uint32_t num_smc,
                                         uint32_t words_per_smc,
                                         std::vector<std::vector<uint8_t>>& smc_banks,
                                         uint32_t col_base = 0u);
    static void pack_scale_dense_interleave(const std::vector<float>& scale_col,
                                            uint32_t num_smc,
                                            uint32_t words_per_smc,
                                            std::vector<std::vector<uint8_t>>& smc_banks,
                                            uint32_t col_base = 0u);

    // 参考模型：单 K_chunk=32 的 bank2 前 4 word（每 word 4×fp32，addr 对应 row 0~3）。
    // k_block=0 → K[0:32]；与 fpadd RMW 写回 layout 一致（Phase-1 RTL 单趟 compute）。
    // M=256：a 为 256×K，用 a_rows=256 + row_base=8g 取组 g 的 8 行（默认 8/0 = M=8 原行为）。
    static bool compute_bank2_golden_kchunk(uint8_t smc_id,
                                            const std::vector<float>& a,
                                            const std::vector<uint8_t>& bq_u4,
                                            const std::vector<uint8_t>& zp_col,
                                            const std::vector<float>& scale_col,
                                            uint32_t k_block,
                                            std::vector<uint8_t>& bank2_out,
                                            uint32_t a_rows = 8u,
                                            uint32_t row_base = 0u,
                                            uint32_t n_stride = 64u,
                                            uint32_t col_base = 0u);

    // ---- fp16×fp16（骨架；IQ 时间轴后续填）----
    // B：32×32 fp16 → 每 SMC 16×128b word（INT4 路径的 4×）；无 zp/scale。
    static void pack_b_fp16_dense_interleave_kblock(const std::vector<uint16_t>& b_fp16,
                                                    uint32_t num_smc,
                                                    uint32_t words_per_smc,
                                                    uint32_t k_block,
                                                    std::vector<std::vector<uint8_t>>& smc_banks,
                                                    uint32_t n_stride = 64u,
                                                    uint32_t col_base = 0u);
    static bool compute_bank2_golden_fp16_kchunk(uint8_t smc_id,
                                                 const std::vector<float>& a,
                                                 const std::vector<float>& b,
                                                 uint32_t k_block,
                                                 std::vector<uint8_t>& bank2_out,
                                                 uint32_t n_stride = 64u,
                                                 uint32_t col_base = 0u);

private:
    MatmulNtW4f16Params parse_request(const app::AppOperatorRequest& req) const;
    static void validate_k(uint32_t k);
    OpInstructionsBase* compile_gemv_m1(const app::AppOperatorRequest& req, const MatmulNtW4f16Params& p);
    /** fp16×fp16：复用链路一/三，旁路链路二与 Phase B×scale（第一版仅 M=8）。 */
    OpInstructionsBase* compile_fp16_gemm(const app::AppOperatorRequest& req, const MatmulNtW4f16Params& p);
    /** GPTQ / fp16 共用 IQ 时间轴；由 para_quantized 分支链路二与 Phase B。 */
    OpInstructionsBase* compile_core(const app::AppOperatorRequest& req, const MatmulNtW4f16Params& p);
    /** M=256 f16 epilogue：bank7（256 fp32 word）逐行对经 UR→VIN→fptofp(dst_pos=0/1)
     *  →VOUT→bank6（128 个 packed f16 word，每 word = 行对 (2r,2r+1) × 4 列）。 */
    OpInstructionsBase* compile_f16_epilogue(const app::AppOperatorRequest& req,
                                             const MatmulNtW4f16Params& p);
};

}  // namespace op
}  // namespace aiacc

#endif

