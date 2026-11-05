#ifndef LSU_OP_BASE_V2_H
#define LSU_OP_BASE_V2_H

#include <cstdint>
#include <iosfwd>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "../op/common/lsu_emit.h"
#include "../op/common/op_instructions_base.h"

namespace aiacc {
namespace lsu {

struct PortInfo {
    uint8_t bank{0};
    uint16_t word_offset{0};
    uint16_t word_count{255};
};

struct LsuComputeDesc {
    std::map<std::string, PortInfo> inputs;
    PortInfo output;

    uint8_t num_smc{8};
    uint8_t smc_id_en{7};
    uint64_t gr_intlv_addr{0x10000000ULL};
    // store 专属 intlv（0 = 跟随 gr_intlv_addr，旧行为）。LDB/STB 两侧 RTL 都用
    // gr_base + smc_id*intlv（ldb_ur_axi_conv.v:488 / stb 同理）：load 必须保持历史值
    // 0x10000000（TB 1MB SRAM 下 alias 到 0）；只有拼接 store 需要 0x20 的小步进。
    uint64_t store_gr_intlv_addr{0};

    // Load/Store 编码模式：
    //   false（默认 loops 模式）：ur_wr/rd_line_cnt=1，靠 IQ 外壳 loops 重复 word_count 次
    //   true （bulk  模式）     ：ur_wr/rd_line_cnt=word_count，一条 LDB/STB 搬完；
    //                            LDBAXI/STBAXI 用能整除 word_count 的 burst 长度精确发，不多发拍
    bool bulk_mode{false};

    // Compatibility shims shared by sub-v422-experiment and upstream main:
    // fields referenced by load_store_v3/sub/sub_lsu_op.cpp and op/mul_scalar
    // (merged sides carried identical declarations; upstream main also notes
    // the committed base struct upstreamed later). Declared so the
    // sub/add/mul_scalar generators build:

    //   store_start_delay/cond : store-start gating knob (collect-only shim;
    //                            base assemble does not consume them yet)
    //   full_ddr_mode/full_*   : SUB_FULL_DDR full-DDR layout params
    uint32_t store_start_delay{0};
    uint8_t  store_start_cond{0};
    bool     full_ddr_mode{false};
    uint64_t full_word_offset{0};
    uint64_t full_total_words{0};
    std::map<std::string, uint64_t> full_input_byte_base;
    uint64_t full_output_byte_base{0};
};

struct LsuCaseConfig {
    std::string case_name;
    std::string case_root;
    std::string hbm_init_dir;
    uint32_t seed_base{42};
};

class LsuOpBase {
public:
    virtual ~LsuOpBase() = default;

    void set_compute_desc(const LsuComputeDesc& desc) { desc_ = desc; }
    void set_case_config(const LsuCaseConfig& cfg) { cfg_ = cfg; }

    op::LsuLoadParams build_load_params(uint8_t bank, uint16_t word_offset,
                                        uint16_t word_count, uint64_t gr_base) const;
    op::LsuStoreParams build_store_params(uint8_t bank, uint16_t word_offset,
                                          uint16_t word_count, uint64_t gr_base) const;

    // Override for custom gather/scatter (e.g. EMBED cat). Default = sequential LDB/STB.
    virtual std::vector<op::IqInstructionFile> generate_load_iq_files() const;
    virtual std::vector<op::IqInstructionFile> generate_store_iq_files() const;

    virtual std::vector<op::IqInstructionFile> generate_compute_iq_files() const = 0;

    virtual void generate_golden_for_smc(
        uint8_t smc_id,
        const std::map<std::string, std::vector<uint8_t>>& input_datas,
        std::vector<uint8_t>& golden_out) const = 0;

    virtual void generate_hbm_data(uint8_t smc_id, uint32_t seed,
                           std::vector<uint8_t>& hbm_bytes) const;

    bool assemble_full_case() const;
    // mm 专用 4.21 真 L-C-S：固定 stage_0=load / stage_1=compute / stage_2=store。
    // 与 assemble_full_case_multistage 独立（后者支持 skip_compute，供其他算子使用）。
    bool assemble_multistage_case() const;
    // v0.4.20/4.21 多段版式:Load=stage_0 / Compute=stage_1 / Store=stage_2,
    // 供 sim_multi_stage 中断流程消费(不影响 4.19 的 assemble_full_case)。
    // skip_compute_stage()==true 时仅写 stage_0=Load / stage_1=Store（无 compute 占位）。
    bool assemble_full_case_multistage() const;
    virtual bool skip_compute_stage() const { return false; }
    // 一次仿真跑 K 个 tile:产 3K 个 stage(每 tile 一组 L-C-S),HBM 按 K-tile 平铺
    // (tile j 的段 gr 偏移 = j*tile_stride)。op 的 generate_hbm_data 需按同布局铺 K 份。
    bool assemble_full_case_multitile(uint32_t tiles_per_sim) const;
    // skip_compute 多 tile：每 tile 2 stage，HBM 按 tile_stride 交错 [A|B|Y]×M。
    bool assemble_full_case_multitile_ls(uint32_t tiles_per_sim) const;
    // skip_compute 多 tile + 大块 resident:
    // DDR = [input0区 M tile 紧排 | input1区 ... | output区 M tile 紧排]
    // 每 tile 2 stage (L/S)；TB 需 +TILE_STAGE_STRIDE=2。
    bool assemble_full_case_resident_ls(uint32_t tiles_per_sim) const;
    uint64_t tile_stride_bytes() const;   // 单 tile 三段(in+other+out)总字节

    // 驻留(连续)布局:一片 DDR = [input区(M tile 紧排) | other区(共享,只存 other_region_words,
    // tile j 读第 (j%other_cycle) 段) | output区(M tile 紧排)],段间不补 0。
    // op 的 generate_hbm_data 需按同布局预置 input区 + other区(output区留 0)。
    bool assemble_full_case_resident(uint32_t M, uint32_t other_cycle,
                                     uint32_t other_region_words) const;

    virtual std::string get_name() const = 0;

protected:
    LsuComputeDesc desc_;
    LsuCaseConfig cfg_;

    static uint16_t compose_ur_addr(uint8_t bank, uint16_t word_addr);
    uint16_t calc_ldb_loops(uint16_t word_count) const;
    uint8_t calc_ldbaxi_loops(uint16_t word_count) const;
    // bulk 模式：返回 <=16 且能整除 word_count 的最大 burst 拍数（凑出精确总拍数）
    static uint16_t largest_burst_beats(uint16_t word_count);

    // bulk 模式 LDBAXI/STBAXI 分段：把 word_count 拍拆成若干 burst 段，
    // 每段 {beats(拍数 2..16), num(连续相同段数,用 loops 压缩), byte_off(相对 gr_base 字节偏移)}。
    // 规则：主体 16 拍 burst；余数 2..15 拍补一段；余数=1 时把末尾 17 拍拆成 9+8（避免 brst=0 单拍）。
    struct BurstSeg { uint16_t beats; uint16_t num; uint64_t byte_off; };
    static std::vector<BurstSeg> bulk_burst_segments(uint16_t word_count);

    // 单条 LDB/STB 的 ur_wr/rd_line_cnt 是 8bit(<=255)。word_count>255(如满 bank 256)时
    // 拆成同一 bank 内多条子指令(每条 <=CHUNK<=128,ur_addr 与 gr 递增),追加到 out。
    // CHUNK=128(16 的倍数,burst 友好;避免出现单拍/超界)。word_count<=CHUNK 时退化为单条。
    static constexpr uint16_t BULK_CHUNK_WORDS = 128;
    void append_bulk_load(std::vector<op::IqInstructionFile>& out,
                          uint8_t bank, uint16_t word_offset,
                          uint16_t word_count, uint64_t gr_base) const;
    void append_bulk_store(std::vector<op::IqInstructionFile>& out,
                           uint8_t bank, uint16_t word_offset,
                           uint16_t word_count, uint64_t gr_base) const;

    uint32_t total_input_words() const;
    uint64_t input_gr_base(const std::string& name) const;
    // virtual：mm 可覆写 store 落点（所有 K 块之后）与密铺 stride（去掉每端口 4KB 对齐）。
    // 默认实现不变，其他算子无需改动。
    virtual uint64_t store_gr_base() const;
    virtual uint64_t seg_stride_bytes(uint16_t word_count) const;

    virtual void fill_input_port_data(const std::string& name,
                                      const PortInfo& port,
                                      uint8_t smc_id,
                                      std::mt19937& rng,
                                      std::vector<uint8_t>& out) const;
    virtual void write_extra_meta_json(std::ostream& os) const;

    // Multitile/resident assemble temporarily overrides GR bases for IQ emit.
    mutable uint64_t gr_tile_offset_{0};
    mutable std::map<std::string, uint64_t> input_gr_override_;
    mutable bool store_gr_override_valid_{false};
    mutable uint64_t store_gr_override_{0};
};

} // namespace lsu
} // namespace aiacc

#endif // LSU_OP_BASE_V2_H
