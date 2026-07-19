#include "bw_and_lsu_op.h"

#include <cstdlib>
#include <cstring>

namespace aiacc {
namespace lsu {

LsuComputeDesc BwAndLsuOp::default_compute_desc() {
    // 元素行数可用环境变量 LSU_WC 覆盖（默认 255），便于验证 bulk 通用性
    uint16_t wc = 255;
    if (const char* e = std::getenv("LSU_WC")) {
        int v = std::atoi(e);
        if (v > 0 && v <= 255) wc = static_cast<uint16_t>(v);
    }
    LsuComputeDesc d;
    d.inputs["input"] = {0, 0, wc};
    d.inputs["other"] = {1, 0, wc};
    d.output = {2, 0, wc};   // bitwise_and outputs to bank2
    d.num_smc = 8;
    d.smc_id_en = 7;
    d.gr_intlv_addr = 0x10000000ULL;
    d.bulk_mode = true;   // LDB/STB 一条搬完 + LDBAXI/STBAXI burst 分段
    return d;
}

std::vector<op::IqInstructionFile> BwAndLsuOp::generate_compute_iq_files() const {
    op::BitwiseAndParams params;
    params.para_n = 1;
    params.para_c = 1;
    params.para_h = 1;
    params.para_w = desc_.output.word_count * 4;  // 255 words * 4 int32/word = 1020 elements
    params.para_dtype = op::BitwiseAndDataType::INT32;

    auto it_input = desc_.inputs.find("input");
    auto it_other = desc_.inputs.find("other");
    if (it_input != desc_.inputs.end()) {
        params.para_input_bank = it_input->second.bank;
        params.para_input_addr = static_cast<uint8_t>(it_input->second.word_offset);
    }
    if (it_other != desc_.inputs.end()) {
        params.para_scalar_bank = it_other->second.bank;
        params.para_scalar_addr = static_cast<uint8_t>(it_other->second.word_offset);
    }
    params.para_output_bank = desc_.output.bank;
    params.para_output_addr = static_cast<uint8_t>(desc_.output.word_offset);

    op::BitwiseAndOp bw_op;
    auto insts = bw_op.compile(params);
    return insts.get_iq_files();
}

void BwAndLsuOp::generate_golden_for_smc(
    uint8_t /*smc_id*/,
    const std::map<std::string, std::vector<uint8_t>>& input_datas,
    std::vector<uint8_t>& golden_out) const {

    auto it_inp = input_datas.find("input");
    auto it_oth = input_datas.find("other");
    if (it_inp == input_datas.end() || it_oth == input_datas.end()) {
        golden_out.clear();
        return;
    }
    const auto& inp = it_inp->second;
    const auto& oth = it_oth->second;

    // bitwise_and 是“标量广播”运算（不是逐元素）：other 的标量 word（scalar_addr 处，
    // 含 4 个 int32 lane）被广播 AND 给 input 的每一个 word，按 lane 对应。
    // 即 golden int32[i] = input int32[i] & scalar_lane[i % 4]。
    uint32_t scalar_word = 0;
    auto it_other_port = desc_.inputs.find("other");
    if (it_other_port != desc_.inputs.end()) scalar_word = it_other_port->second.word_offset;
    uint32_t scalar_byte = scalar_word * 16;

    uint32_t scalar_lane[4] = {0, 0, 0, 0};
    for (int k = 0; k < 4; ++k) {
        if (scalar_byte + k * 4 + 4 <= oth.size())
            std::memcpy(&scalar_lane[k], &oth[scalar_byte + k * 4], 4);
    }

    uint32_t num_u32 = static_cast<uint32_t>(inp.size()) / 4;
    golden_out.resize(num_u32 * 4);

    for (uint32_t i = 0; i < num_u32; ++i) {
        uint32_t a = 0;
        std::memcpy(&a, &inp[i * 4], 4);
        uint32_t r = a & scalar_lane[i % 4];   // 标量广播
        std::memcpy(&golden_out[i * 4], &r, 4);
    }
}

} // namespace lsu
} // namespace aiacc
