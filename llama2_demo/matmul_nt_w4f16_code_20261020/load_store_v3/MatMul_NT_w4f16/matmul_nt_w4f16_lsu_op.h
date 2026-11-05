#ifndef MATMUL_NT_W4F16_LSU_OP_H
#define MATMUL_NT_W4F16_LSU_OP_H

#include "../lsu_op_base.h"
#include "../../op/matmul_nt_w4f16/matmul_nt_w4f16_op.h"

namespace aiacc {
namespace lsu {

// mm 动态 K：LDB 按 K 块换入 A/B，同一 K_chunk compute body 外循环 num_k_blocks 次。
// M=8：K_chunk=32；M=1（MM_M=1）：K_chunk=256（见 MM_M1_PLAN.md）。
// MM_K_BATCH>1 时一次 LDB 灌多块到 bank 连续地址，再连续多段 compute（换 a_addr/b_addr）。
class MatmulNtW4f16LsuOp : public LsuOpBase {
public:
    MatmulNtW4f16LsuOp() = default;
    ~MatmulNtW4f16LsuOp() override = default;

    std::string get_name() const override { return "MatMul_NT_w4f16"; }

    std::vector<op::IqInstructionFile> generate_compute_iq_files() const override;

    // M=256 f16（D5）：bank6 的 128 个 packed f16 word → 64 个 256B 块（每块 4 行 × 32 列），
    // 每 SMC 一个 2-word(32B) burst，块间地址 +256B，靠 gr_intlv_addr=0x20 拼成连续 256B。
    // 其余形状走基类原路径（M=8：bank7 的 8 个 fp32 word）。
    std::vector<op::IqInstructionFile> generate_store_iq_files() const override;

    // M=256 epilogue：bank7(para_ep_src_addr 起 words*2 行) → bank6(base 起 words 个 packed
    // f16 word)。word_base 为 bank6 字基址，同时决定 bank7 行基址 = 2*word_base。
    std::vector<op::IqInstructionFile> generate_f16_epilogue_iq_files(uint32_t word_base,
                                                                     uint32_t words) const;

    // M=256（D6）：从 HBM 零区 LDB 256 个零 word 进 bank7，保证 FPGA 逐 tile 跑时累加器干净
    // （RTL 单 case 下 ur_data/bank7 初始即为零，无副作用）。
    std::vector<op::IqInstructionFile> generate_bank7_clear_iq_files() const;

    // 指定 UR 读基址与 k_block（batch 内第 i 块：a_addr=i*a_words，b_addr=i*b_words；
    // fp16 下二者不同，不可共用 a 步长）。
    // zp_addr/scale_addr：本 batch 内 group 槽位基址（通常 = local_group * kZpWordsPerSmc）。
    // out_addr：bank2/bank7 行基址（M=256 组 g → g*8；其余 0，D1' 组=独立 stage）。
    std::vector<op::IqInstructionFile> generate_compute_iq_files_for_k_block(
        uint8_t a_addr, uint8_t b_addr, uint32_t k_block,
        uint8_t zp_addr = 0, uint8_t scale_addr = 0, uint8_t out_addr = 0) const;

    void generate_golden_for_smc(
        uint8_t smc_id,
        const std::map<std::string, std::vector<uint8_t>>& input_datas,
        std::vector<uint8_t>& golden_out) const override;

    void generate_hbm_data(uint8_t smc_id, uint32_t seed,
                           std::vector<uint8_t>& hbm_bytes) const;

    bool assemble_full_case() const;

    // 多 stage 落地版式（真 LDB load / compute / 真 STB store 三阶段）：
    //   smi_iq_inst/stage_0 = load(LDB/LDBAXI)、stage_1 = compute、stage_2 = store(STB/STBAXI)。
    // 由 tb_aiacc_top_v04_f_multi_stage.v 逐阶段等 smi_iq_done 顺序执行。
    // 通过环境变量 MM_MULTISTAGE=1 时，assemble_full_case 会委托到这里。
    bool assemble_multistage_case() const;

    static LsuComputeDesc default_compute_desc();

private:
    // zp/scale：32 列按 2-word 间插到 8 SMC → 每 SMC 拥有 4 列 = 4 words（每 word 复制满字）。
    static constexpr uint32_t kZpWordsPerSmc = 4u;
    static constexpr uint32_t kScaleWordsPerSmc = 4u;

    /** 1 = decode GEMV；8 = 现网 GEMM；256 = prefill（32 row-group 循环）。读环境变量 MM_M。 */
    uint32_t m_rows() const;
    bool is_m1() const { return m_rows() == 1u; }
    bool is_m256() const { return m_rows() == op::kMatmulNtW4f16M256; }
    /** f16 packed 输出（bank6 + epilogue + 拼接 store）：M=256 恒 true；
     *  M=8 由 MM_OUT_DTYPE=f16 打开（阶梯 1 快验用）；M=1 暂不支持（Phase 6）。 */
    bool out_f16() const;
    /** MatMul_NT_w4f16：恒 GPTQ INT4，不读 MM_QUANT。 */
    bool is_fp16() const;
    /** M=1 → 256；M=8 → 32。 */
    uint32_t k_chunk() const;
    /** M=1 replicate：每 SMC 32 word；M=8：4 word。 */
    uint32_t a_words_per_smc() const;
    /** GPTQ M=8：4；GPTQ M=1：32；fp16 M=8：16（INT4 的 4×）。 */
    uint32_t b_words_per_smc() const;
    /** M=1：2 word（见下）；M=8：8 word。 */
    uint32_t out_words() const;

    uint32_t k_total() const;
    uint32_t num_k_blocks() const;

    // ---- N-tile 列窗（Phase 4 / D3）----
    /** 全局 N 列数：B/zp/scale 文件宽。环境变量 MM_N，默认 64。 */
    uint32_t n_total() const;
    /** 本 case 的 N-tile 序号。环境变量 MM_N_TILE_START，默认 0。 */
    uint32_t n_tile_idx() const;
    /** 本 case 打包的全局列起点 = n_tile_idx*32（32 列/tile）。 */
    uint32_t col_base() const;

    // ---- HBM 槽位图（Phase 4 / D7，2026-09-30 固定全局图改版）----
    /** MM_SLOT_MAP=fpga：FPGA 固定全局槽位（每槽 256MB 间隔，与 FPGA 侧配置一致）：
     *  in0=A@0x0, in1=B@0x10000000, in2=scale@0x20000000, in3=zp@0x30000000,
     *  in4=zeros@0x40000000（bank7 清零区），out0=store@0x70000000（store intlv 隐式 0x20，
     *  8×32B 拼 256B 连续块）。每输入全 HBM 单拷贝：8 个 SMC 切片按统一步长 S 打包在槽内，
     *  SMC s 位于 slot_base + s*S；load gr_intlv_addr=S，硬件 +smc*intlv 自动选片。
     *  默认 rtl：紧凑布局（tensor-major 区域密铺，全 <1MB 供 TB）。static：default_compute_desc 也用。 */
    static bool slot_map_fpga();

    // ---- HBM tensor-major 布局（每输入张量一个连续区域，区域内按 K 块密铺）----
    // 区域 stride = align256(num_k_blocks × word_count × 16)；区域基址 256B 对齐。
    // AXI 合法性：burst ≤ 256B，区域基址 256B 对齐 + 块内步长整除 4096 → 任何 burst 不跨 4KB 页。
    uint64_t region_stride_bytes(uint16_t word_count) const;
    // 张量区域基址（IQ/LDB 用的真实 DDR 地址）：rtl 模式 = 前序区域 stride 累加；
    // fpga 模式 = 固定全局槽位（见 slot_map_fpga 注释）。端口顺序同 desc_.inputs 字典序 a<b<scale<zp。
    uint64_t tensor_region_base(const std::string& name) const;
    // memory_init 镜像内的紧凑偏移（与 rtl 模式一致；fpga 模式镜像保持紧凑，由 packer 按
    // 槽位重绑定，meta 同时给出 byte_base（槽位）与 image_byte_base（镜像偏移））。
    uint64_t image_region_base(const std::string& name) const;
    // 第 kb 个 K 块的字节地址 = 区域基址 + kb × word_count×16。
    uint64_t tensor_block_base(const std::string& name, uint32_t kb) const;
    // 量化 group 大小（外部张量或默认 64）。
    uint32_t quant_group_size() const;
    // 一次 LDB 灌入的 K 块数；环境变量 MM_K_BATCH，默认 4（M=1 默认 1）。
    // zp/scale 按 group 多槽写入 bank4/5（槽距 kZpWordsPerSmc），可跨 group。
    uint32_t k_batch_size() const;

    // store 落点 = 所有输入区域之后（各区域 256B 对齐，末端天然对齐）。
    // UR 侧：每块 Load 覆盖写到相同 bank 基址；HBM 侧：各块在各自张量区域内有独立偏移。
    uint64_t store_gr_base() const override;

    // M=256（D6）：bank7 清零用的 HBM 零区（store 区之后 64KB，避免与 store 落点互相覆盖）。
    uint64_t zero_region_base() const;

    std::vector<op::IqInstructionFile> generate_load_iq_files_for_k_block(uint32_t k_block) const;

    // 将 kb_begin .. kb_begin+batch_n-1 的 A/B 写入 bank 连续地址；
    // zp/scale 按本 batch 内相对 group 槽写入 bank4/5（槽 s → word s*4）。
    std::vector<op::IqInstructionFile> generate_load_iq_files_for_k_batch(
        uint32_t kb_begin, uint32_t batch_n) const;

    void append_load_port_iq(std::vector<op::IqInstructionFile>& all,
                             uint8_t bank,
                             uint16_t ur_word_offset,
                             uint16_t word_count,
                             uint64_t gr_base,
                             uint8_t inst_id) const;

    uint16_t compute_body_cycles() const;
    uint16_t load_block_cycles() const;
    uint16_t compute_after_load_gap() const;

    void build_test_tensors(uint8_t smc_id, uint32_t seed,
                            std::vector<float>& a,
                            std::vector<uint8_t>& bq_u4,
                            std::vector<uint8_t>& zp_bank0,
                            std::vector<uint8_t>& scale_bank0) const;
    void build_test_tensors(uint8_t smc_id, uint32_t seed,
                            std::vector<float>& a,
                            std::vector<uint8_t>& bq_u4,
                            std::vector<float>& b_fp32,
                            std::vector<uint8_t>& zp_bank0,
                            std::vector<uint8_t>& scale_bank0) const;

    // 从算子请求或外部张量目录取 per-列 zp/scale（N=64）。
    // k_block>0 且为外部 GPTQ 数据时，选取 scale_group=(k_block*k_chunk)/group_size 那一组。
    void extract_zp_scale_cols(std::vector<uint8_t>& zp_col,
                               std::vector<float>& scale_col,
                               uint32_t k_block = 0) const;

    struct ExternalTensors {
        bool attempted = false;
        bool valid = false;
        std::vector<float> a;
        std::vector<uint8_t> bq;
        std::vector<float> b_fp32;  // fp16 模式稠密 B（K×64）
        std::vector<uint8_t> zp;    // [num_groups, 64] row-major u4
        std::vector<float> scale;   // [num_groups, 64] row-major fp32
        uint32_t group_size = 64u;
        uint32_t num_groups = 1u;
    };
    void ensure_external_loaded() const;
    mutable ExternalTensors ext_;
};

}  // namespace lsu
}  // namespace aiacc

#endif  // MATMUL_NT_W4F16_LSU_OP_H
