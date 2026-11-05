#include "rtovr_iq.h"
#include <cstring>
#include <stdexcept>

namespace aiacc {
namespace iq {

void RtoVrIq::pack_cru_rtovr_words(const RtoVrCru& para_cru, uint32_t out_words[10]) {
    // 1. 严格初始化 10 个 words (320 bits)
    for (int i = 0; i < 10; ++i) out_words[i] = 0;

    // 如果全局无效，保持全 0
    if (!para_cru.para_vld) return;

    // 2. 遍历 40 组控制单元
    for (int k = 0; k < 40; ++k) {
        // 组内布局：mv_out (6位) + vld (1位) = 7 bits
        // vld 位于 bit 0
        const uint8_t sel = (uint8_t)(para_cru.para_out[k].para_select & 0x3Fu); // 6位选择码 (0-63)
        const uint8_t vld = (uint8_t)(para_cru.para_out[k].para_valid ? 1u : 0u);
        const uint8_t cw = (uint8_t)((sel << 1) | vld); 

        // 3. 计算位置：逆序压入 (Group 0 在 Bit 279:273)
        const int msb = 279 - 7 * k;
        const int lsb = msb - 6;

        // 4. 将 7 bits 逐位映射
        for (int b = 0; b < 7; ++b) {
            const int bit_idx = lsb + b; // 范围 0 - 279
            const uint32_t bit_val = (cw >> b) & 1u;
            
            const int word_idx = bit_idx / 32;
            const int bit_in_word = bit_idx % 32;
            
            if (word_idx < 10) {
                if (bit_val) {
                    out_words[word_idx] |= (1u << bit_in_word);
                }
            }
        }
    }
}

void RtoVrIq::make_micro_insts(const RtoVrCru& para_cru, MicroInstruction out_insts[10]) {
    uint32_t words[10] = {0};
    
    // 生成打包数据
    pack_cru_rtovr_words(para_cru, words);

    // 将 10 个打包好的 word 填入微指令序列
    // 硬件将顺序接收这 10 条指令并重组为 280-bit 向量
    for (int i = 0; i < 10; ++i) {
        MicroInstruction inst{};
        inst.vld = para_cru.para_vld;
        inst.code = words[i];
        out_insts[i] = inst;
    }
}

} // namespace iq
} // namespace aiacc
