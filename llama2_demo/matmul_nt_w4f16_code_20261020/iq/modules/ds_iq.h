#ifndef DS_IQ_H
#define DS_IQ_H

/**
 * @file ds_iq.h
 * @brief DS (Data Shuffle) 模块 IQ 微指令编码。
 *
 * ISA v0.4.9 §7.3 / 表 5.83 定义 DS 模块只有一条上行指令流（39 bit）。
 * DS 坐落在 STB/LDBAXI -> LDB/STBAXI 之间的 AXI Stream 上，实现两类数据整理：
 *   - Reshape   (op_type = 0)：不改数据内容，只重贴 TLAST 边界，改变
 *                               逻辑"行"宽度（r_in_cnt -> r_out_cnt）。
 *   - Transpose (op_type = 1)：在内部 PARAM_TRANSPOSE_SIZE × PARAM_TRANSPOSE_SIZE
 *                               (V1.0 = 32×32) 的 buffer 里物理重排元素；
 *                               超出此尺寸须由 compiler 切 tile。
 *
 * 注意：
 *   - 若 tid 设为 pipeline bypass (STB/LDBAXI tid = b00/b10)，DS 仅做透传，
 *     此时 DS 指令可保持 vld=0。
 *   - V1.0 不支持 Scatter/Gather 与 Reduce，不在本模块编码范围。
 */

#include <cstdint>
#include "../smi_iq_base.h"

namespace aiacc {
namespace iq {

// ==========================================================================
// DS 上行微指令 (表 5.83)    -> 39 bit
// ==========================================================================
//  Bit layout (LSB first):
//   [0]       vld             1  指令有效
//   [1]       op_type         1  0=reshape / 1=transpose
//   [3:2]     data_type       2  切片元素精度:
//                                   b00 = 4 bit
//                                   b01 = 8 bit
//                                   b10 = 16 bit
//                                   b11 = 32 bit
//   [11:4]    r_in_cnt        8  reshape 输入行每行元素数
//   [19:12]   r_out_cnt       8  reshape 输出行每行元素数
//   [24:20]   t_dim0_cnt      5  transpose dim0 大小 (0 -> 1, 31 -> 32)
//   [29:25]   t_dim1_cnt      5  transpose dim1 大小 (0 -> 1, 31 -> 32)
//   [33:30]   inst_id         4
//   [38:34]   smc_id_en       5

enum class DsOpType : uint8_t {
    RESHAPE   = 0x0,
    TRANSPOSE = 0x1,
};

enum class DsDataType : uint8_t {
    BIT4  = 0x0,
    BIT8  = 0x1,
    BIT16 = 0x2,
    BIT32 = 0x3,
};

struct DsCru {
    bool       para_vld{false};
    DsOpType   para_op_type{DsOpType::RESHAPE};
    DsDataType para_data_type{DsDataType::BIT32};
    uint8_t    para_r_in_cnt{0};    // 8 bit (reshape)
    uint8_t    para_r_out_cnt{0};   // 8 bit (reshape)
    uint8_t    para_t_dim0_cnt{0};  // 5 bit (transpose, ISA: 0 -> size 1)
    uint8_t    para_t_dim1_cnt{0};  // 5 bit (transpose)
    uint8_t    para_inst_id{0};     // 4 bit
    uint8_t    para_smc_id_en{0};   // 5 bit
};

class DsIq : public SmiIqBase {
public:
    explicit DsIq(SmiIqHandle* para_handle) : SmiIqBase(para_handle) {}

    // DS 上行微指令 (39 bit)，填 out_words[0..1]
    static void pack_cru_ds(const DsCru& para_cru, uint32_t out_words[2]);

    static MicroInstruction make_micro_inst(const DsCru& para_cru);

    // 便捷构造：Reshape（只改逻辑行宽度）
    static DsCru make_reshape(DsDataType dtype,
                              uint8_t    in_elems_per_line,
                              uint8_t    out_elems_per_line,
                              uint8_t    inst_id   = 0,
                              uint8_t    smc_id_en = 0);

    // 便捷构造：Transpose（dim0 × dim1，size 传 1..32 的实际元素数）
    // ISA 字段语义：field = size - 1。size 必须在 [1, PARAM_TRANSPOSE_SIZE]。
    static DsCru make_transpose(DsDataType dtype,
                                uint8_t    dim0_size,
                                uint8_t    dim1_size,
                                uint8_t    inst_id   = 0,
                                uint8_t    smc_id_en = 0);
};

} // namespace iq
} // namespace aiacc

#endif
