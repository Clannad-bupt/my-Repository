#ifndef LSU_OP_BASE_V2_H
#define LSU_OP_BASE_V2_H

#include <cstdint>
#include <map>
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

    // Load/Store 缂栫爜妯″紡锛?    //   false锛堥粯璁?loops 妯″紡锛夛細ur_wr/rd_line_cnt=1锛岄潬 IQ 澶栧３ loops 閲嶅 word_count 娆?    //   true 锛坆ulk  妯″紡锛?    锛歶r_wr/rd_line_cnt=word_count锛屼竴鏉?LDB/STB 鎼畬锛?    //                            LDBAXI/STBAXI 鐢ㄨ兘鏁撮櫎 word_count 鐨?burst 闀垮害绮剧‘鍙戯紝涓嶅鍙戞媿
    bool bulk_mode{false};
    uint32_t store_start_delay{0};
    uint8_t store_start_cond{0};

    bool full_ddr_mode{false};
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

    std::vector<op::IqInstructionFile> generate_load_iq_files() const;
    std::vector<op::IqInstructionFile> generate_store_iq_files() const;
    static void prepend_start_delay(op::IqInstructionFile& file, uint32_t delay_cycles);

    virtual std::vector<op::IqInstructionFile> generate_compute_iq_files() const = 0;

    virtual void generate_golden_for_smc(
        uint8_t smc_id,
        const std::map<std::string, std::vector<uint8_t>>& input_datas,
        std::vector<uint8_t>& golden_out) const = 0;

    virtual void generate_hbm_data(uint8_t smc_id, uint32_t seed,
                           std::vector<uint8_t>& hbm_bytes) const;

    bool assemble_full_case() const;

    virtual std::string get_name() const = 0;

protected:
    LsuComputeDesc desc_;
    LsuCaseConfig cfg_;

    static uint16_t compose_ur_addr(uint8_t bank, uint16_t word_addr);
    uint16_t calc_ldb_loops(uint16_t word_count) const;
    uint8_t calc_ldbaxi_loops(uint16_t word_count) const;
    // bulk 妯″紡锛氳繑鍥?<=16 涓旇兘鏁撮櫎 word_count 鐨勬渶澶?burst 鎷嶆暟锛堝噾鍑虹簿纭€绘媿鏁帮級
    static uint16_t largest_burst_beats(uint16_t word_count);

    struct BurstSeg {
        uint16_t beats;
        uint16_t num;
        uint64_t byte_off;
    };
    static std::vector<BurstSeg> bulk_burst_segments(uint16_t word_count);
    static std::vector<BurstSeg> bulk_burst_segments_at_base(uint64_t byte_base, uint16_t word_count);
    static uint16_t bulk_total_beats(uint16_t word_count);
    uint32_t total_input_words() const;
    uint64_t input_gr_base(const std::string& name) const;
    uint64_t store_gr_base() const;
    uint64_t seg_stride_bytes(uint16_t word_count) const;
};

} // namespace lsu
} // namespace aiacc

#endif // LSU_OP_BASE_V2_H

