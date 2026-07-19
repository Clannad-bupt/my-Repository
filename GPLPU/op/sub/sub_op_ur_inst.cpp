#include "sub_op_ur_inst.h"
#include "sub_op.h"

namespace aiacc {
namespace op {

// -----------------------------------------------------------------------------
// UR 微指令生成（读 / 写）
// -----------------------------------------------------------------------------

void sub_gen_ur_read_inst(const SubOpParams& para_params,
                      uint32_t para_step,
                      iq::UrCru& para_cru) {
    para_cru.para_vld = true;
    
    // alpha 广播：始终读同一地址
    const uint32_t alpha_step = 0;
    
    // 读端口 0: input
    para_cru.para_raddr[0].bank = para_params.para_input_bank;
    para_cru.para_raddr[0].addr_word = para_params.para_input_addr + para_step;
    para_cru.para_re[0] = true;
    
    // 读端口 1: other
    para_cru.para_raddr[1].bank = para_params.para_other_bank;
    para_cru.para_raddr[1].addr_word = para_params.para_other_addr + para_step;
    para_cru.para_re[1] = true;
    
    // 读端口 2: alpha (广播)
    para_cru.para_raddr[2].bank = para_params.para_alpha_bank;
    para_cru.para_raddr[2].addr_word = para_params.para_alpha_addr + alpha_step;
    para_cru.para_re[2] = true;
    
    // 读端口 3: 不使用
    para_cru.para_re[3] = false;
    
    // 写端口: 不使用 (读指令)
    para_cru.para_we[0] = false;
    para_cru.para_we[1] = false;
    para_cru.para_we[2] = false;
    para_cru.para_we[3] = false;
}

void sub_gen_ur_read_inst_slot(const SubOpParams& para_params,
                           uint32_t para_step,
                           uint8_t slot_id,
                           const SubOpInstructions::SlotAccess& slot_access,
                           iq::UrCru& para_cru) {
    para_cru.para_vld = true;
    
    // 初始化所有端口为无效
    para_cru.para_re[0] = false;
    para_cru.para_re[1] = false;
    para_cru.para_re[2] = false;
    para_cru.para_re[3] = false;
    para_cru.para_we[0] = false;
    para_cru.para_we[1] = false;
    para_cru.para_we[2] = false;
    para_cru.para_we[3] = false;
    
    // 根据 slot_access 信息，只启用该 slot 需要的读端口
    // alpha 广播：始终读同一地址
    const uint32_t alpha_step = 0;
    
    // 读端口 0: input（如果该 slot 需要读 input）
    if (slot_access.input_slot == slot_id) {
        para_cru.para_raddr[0].bank = para_params.para_input_bank;
        para_cru.para_raddr[0].addr_word = para_params.para_input_addr + para_step;
        para_cru.para_re[0] = true;
    }
    
    // 读端口 1: other（如果该 slot 需要读 other）
    if (slot_access.other_slot == slot_id) {
        para_cru.para_raddr[1].bank = para_params.para_other_bank;
        para_cru.para_raddr[1].addr_word = para_params.para_other_addr + para_step;
        para_cru.para_re[1] = true;
    }
    
    // 读端口 2: alpha（如果该 slot 需要读 alpha）
    if (slot_access.alpha_slot == slot_id) {
        para_cru.para_raddr[2].bank = para_params.para_alpha_bank;
        para_cru.para_raddr[2].addr_word = para_params.para_alpha_addr + alpha_step;
        para_cru.para_re[2] = true;
    }
}

void sub_gen_ur_write_inst(const SubOpParams& para_params,
                       uint32_t para_step,
                       iq::UrCru& para_cru) {
    para_cru.para_vld = true;
    
    // 读端口: 不使用（纯写指令）
    para_cru.para_re[0] = false;
    para_cru.para_re[1] = false;
    para_cru.para_re[2] = false;
    para_cru.para_re[3] = false;
    
    // 写端口 0: output
    para_cru.para_waddr[0].bank = para_params.para_output_bank;
    para_cru.para_waddr[0].addr_word = para_params.para_output_addr + para_step;
    para_cru.para_we[0] = true;
    para_cru.para_wbe[0] = 0xFFFF;  // 全字节使能
    
    // 写端口 1/2/3: 不使用
    para_cru.para_we[1] = false;
    para_cru.para_we[2] = false;
    para_cru.para_we[3] = false;
}

void sub_gen_ur_write_inst_slot(const SubOpParams& para_params,
                            uint32_t para_step,
                            uint8_t slot_id,
                            const SubOpInstructions::SlotAccess& slot_access,
                            iq::UrCru& para_cru) {
    para_cru.para_vld = true;
    
    // 初始化所有端口为无效
    para_cru.para_re[0] = false;
    para_cru.para_re[1] = false;
    para_cru.para_re[2] = false;
    para_cru.para_re[3] = false;
    para_cru.para_we[0] = false;
    para_cru.para_we[1] = false;
    para_cru.para_we[2] = false;
    para_cru.para_we[3] = false;
    
    // 根据 slot_access 信息，只启用该 slot 需要的写端口
    // 写端口 0: output（如果该 slot 需要写 output）
    if (slot_access.output_slot == slot_id) {
        para_cru.para_waddr[0].bank = para_params.para_output_bank;
        para_cru.para_waddr[0].addr_word = para_params.para_output_addr + para_step;
        para_cru.para_we[0] = true;
        para_cru.para_wbe[0] = 0xFFFF;  // 全字节使能
    }
}

} // namespace op
} // namespace aiacc
