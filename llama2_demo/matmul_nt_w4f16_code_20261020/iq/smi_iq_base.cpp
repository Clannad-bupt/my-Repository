#include "smi_iq_base.h"

/**
 * @file smi_iq_base.cpp
 * @brief IQ 基类实现：实现 push/容量检查/字段检查，并将结果写入可选的软件 sink。
 */

namespace aiacc {
namespace iq {

using aiacc::hal::check_bits_s;
using aiacc::hal::check_bits_u;

SmiIqBase::SmiIqBase(SmiIqHandle* para_handle) : para_handle_(para_handle) {
    if (!para_handle_) {
        throw std::runtime_error("SmiIqBase: handle is null");
    }
}

void SmiIqBase::check_instq_capacity_or_throw(uint32_t para_new_count) const {
    const uint32_t cur = para_handle_->current_inst_cnt;
    const uint32_t depth = para_handle_->config.para_instq_depth;
    if (depth != 0 && (cur + para_new_count) > depth) {
        throw std::runtime_error("SmiIqBase: instq overflow");
    }
}

void SmiIqBase::check_micro_inst_fields_or_throw(const MicroInstruction& para_inst) const {
    const auto& cfg = para_handle_->config;

    if (cfg.para_en_embed_loops) {
        check_bits_u(para_inst.embed, cfg.para_bits_embed_loops, "EMBED");
    }
    if (cfg.para_en_loops) {
        check_bits_u(para_inst.loops, cfg.para_bits_loops, "LOOPS");
    }
    if (cfg.para_en_cond) {
        check_bits_u(para_inst.cond, cfg.para_bits_cond, "COND");
    }
    if (cfg.para_en_dly) {
        check_bits_u(para_inst.dly, cfg.para_bits_dly, "DLY");
    }
    if (cfg.para_en_inc) {
        check_bits_s(para_inst.inc, cfg.para_bits_inc, "INC");
    }

    check_bits_u(para_inst.code, cfg.para_bits_code, "CODE");
}

void SmiIqBase::push_micro_inst(const MicroInstruction& para_inst) {
    check_instq_capacity_or_throw(1);
    check_micro_inst_fields_or_throw(para_inst);

    micro_insts_.push_back(para_inst);
    if (para_handle_->para_sw_inst_sink) {
        para_handle_->para_sw_inst_sink->push_back(para_inst);
    }
    para_handle_->current_inst_cnt += 1;
}

} // namespace iq
} // namespace aiacc
