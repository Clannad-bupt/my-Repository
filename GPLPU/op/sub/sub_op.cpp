/**
 * @file sub_op.cpp
 * @brief Sub 算子核心实现
 * 功能：out = input - alpha * other
 */
 #include "sub_op.h"
 #include <iostream>
 #include <iomanip>
 #include <cstring>
 #include <algorithm>
 #include <stdexcept>
 #include <type_traits>
 #include <map>
 #include <set>
 #include "../../iq/iq_id_defs.h"
 #include "../../app/fp16_utils.h"
 #include "ur_code_extractor.h"
 
 #include "sub_op_constants.h"
 #include "sub_op_bank_conflict.h"
 #include "sub_op_ur_inst.h"
 #include "sub_op_rtovr_inst.h"
 #include "sub_op_compute_inst.h"
 
 namespace aiacc {
 namespace op {
 
 /**
  * @brief Internal constants/helpers for instruction packing.
  */
 namespace {
 constexpr uint8_t kMaxDlyField = 7;  ///< 3-bit DLY field max value
 constexpr uint32_t kRtovrPorts = 40;
 constexpr uint32_t kRtovrBitsPerPort = 7;
 constexpr uint32_t kRtovrTotalBits = kRtovrPorts * kRtovrBitsPerPort;  // 280
 constexpr uint32_t kRtovrMsbBitIndex = kRtovrTotalBits - 1;            // 279
 
 inline uint32_t rtovr_consumer_cycle(uint32_t route_latch_cycle) {
     return route_latch_cycle ;
 }
 
 inline uint32_t rtovr_writeback_cycle(uint32_t route_latch_cycle) {
     return route_latch_cycle + 3;
 }
 
 struct SubSteadyStateSchedule {
     static constexpr uint32_t kPipelineDrain = 13;

     SubSteadyStateSchedule() {
         // Stage 1: FPMUL 输入
         ur_r1 = 0; // other
         ur_r2 = 0; // alpha
         rtovr_fpmul_s0 = 0;
         rtovr_fpmul_s1 = 0;

         fpmul = 1; 

         // Stage 2: FPADD 输入
         fpadd = 5;
         
         ur_r0 = 4;
         
         rtovr_fpadd_s0 = 5;
         rtovr_fpadd_s1 = 5;

         // Stage 3: UVR 写回
         rtovr_uwr0 = 9;
         ur_w0 = 11; 
     }

     uint32_t ur_r1;
     uint32_t ur_r2;
     uint32_t rtovr_fpmul_s0;
     uint32_t rtovr_fpmul_s1;
     uint32_t fpmul;
     uint32_t ur_r0;
     uint32_t rtovr_fpadd_s0;
     uint32_t rtovr_fpadd_s1;
     uint32_t fpadd;
     uint32_t rtovr_uwr0;
     uint32_t ur_w0;
 };
 
 template <typename EmitDummyFn>
 uint8_t emit_dummy_for_long_delay(uint32_t& total_dly, EmitDummyFn&& emit_dummy) {
      while (total_dly > kMaxDlyField) {
          emit_dummy();
          total_dly -= 8;
      }
      return static_cast<uint8_t>(total_dly);
  }
 
  template <typename EmitLongDummyFn, typename EmitPreciseDummyFn>
  void consume_startup_delay(uint32_t& total_dly,
                             EmitLongDummyFn&& emit_long_dummy,
                             EmitPreciseDummyFn&& emit_precise_dummy) {
      emit_dummy_for_long_delay(total_dly, std::forward<EmitLongDummyFn>(emit_long_dummy));
      if (total_dly > 0) {
          emit_precise_dummy(static_cast<uint8_t>(total_dly - 1));
          total_dly = 0;
      }
  }
 } // namespace
 
 static void sub_gen_ur_insts_impl(const SubOpParams& params, SubOpInstructions& result) {
     if (result.para_num_slots == 1) {
         // 无冲突：单 slot 模式
         iq::UrCru ur_read_cru;
         sub_gen_ur_read_inst(params, 0, ur_read_cru);
         result.para_ur_read_crus.push_back(ur_read_cru);
         
         iq::UrCru ur_write_cru;
         sub_gen_ur_write_inst(params, 0, ur_write_cru);
         result.para_ur_write_crus.push_back(ur_write_cru);
     } else {
         // 有冲突：多 slot 模式
         {
             iq::UrCru ur_read_cru;
             sub_gen_ur_read_inst(params, 0, ur_read_cru);
             result.para_ur_read_crus.push_back(ur_read_cru);
             
             iq::UrCru ur_write_cru;
             sub_gen_ur_write_inst(params, 0, ur_write_cru);
             result.para_ur_write_crus.push_back(ur_write_cru);
         }
     }
 }

 
 static void gen_float_compute_impl(const SubOpParams& params, SubOpInstructions& result) {
     // Stage1: alpha, other → FPMUL
     {
         iq::RtoVrCru rtovr_cru;
         sub_gen_rtovr_to_fpmul(rtovr_cru);
         uint32_t rtovr_words[10];
         iq::RtoVrIq::pack_cru_rtovr_words(rtovr_cru, rtovr_words);
         result.para_rtovr_stage1.assign(rtovr_words, rtovr_words + 9);
     }
     
     // Stage2: input, fpmul_d → FPADD
     {
         iq::RtoVrCru rtovr_cru;
         sub_gen_rtovr_to_fpadd(rtovr_cru);
         uint32_t rtovr_words[10];
         iq::RtoVrIq::pack_cru_rtovr_words(rtovr_cru, rtovr_words);
         result.para_rtovr_stage2.assign(rtovr_words, rtovr_words + 9);
     }
     
     // Stage3: fpadd_d → UVR_WDATA
     {
         iq::RtoVrCru rtovr_cru;
         sub_gen_rtovr_fpadd_to_ur(rtovr_cru);
         uint32_t rtovr_words[10];
         iq::RtoVrIq::pack_cru_rtovr_words(rtovr_cru, rtovr_words);
         result.para_rtovr_stage3.assign(rtovr_words, rtovr_words + 9);
     }
     
     // 生成 FPMUL 和 FPADD 指令
     {
         iq::FpMulCru fpmul_cru;
         sub_gen_fpmul_inst(params, fpmul_cru);
         result.para_fpmul_inst = iq::FpMulIq::pack_cru_fpmul(fpmul_cru);
     }
     {
         iq::FpAddCru fpadd_cru;
         sub_gen_fpadd_inst(params, fpadd_cru);
         result.para_fpadd_inst = iq::FpAddIq::pack_cru_fpadd(fpadd_cru);
     }
 }
 
 SubOpInstructions SubOp::compile(const SubOpParams& para_params) {
     SubOpInstructions result;
     result.para_dtype = para_params.para_dtype;
     
     // ========================================================================
     // 1. 解决 Bank 冲突，分配时间槽（slot）
     // ========================================================================
     result.para_num_slots = aiacc::op::sub_compute_slot_allocation(
         para_params, result.para_slot_accesses, result.para_port_slots);
     
     if (result.para_num_slots > 1) {
         std::cout << "[Info] Bank conflict detected (num_slots=" 
                   << (int)result.para_num_slots 
                   << "), using EMBED nested-loop scheduling.\n";
     }
     
     // 2. 计算循环参数
     const uint32_t total_elems = para_params.para_n * para_params.para_c * 
                                  para_params.para_h * para_params.para_w;
     const uint32_t elems_per_word = para_params.get_elems_per_word();
     const uint32_t total_steps = (total_elems + elems_per_word - 1) / elems_per_word;
     
     result.para_total_steps = total_steps;
     result.para_total_elements = total_elems;
     
     // 嵌套循环：外层 = N*C*H，内层 = W
     const uint32_t w_steps = para_params.para_w / elems_per_word;
     const uint32_t outer_loops = para_params.para_n * para_params.para_c * para_params.para_h;
     result.para_embed_loops = outer_loops;
     result.para_inner_loops = w_steps;
     
     // 3. 生成 UR 读写指令
     sub_gen_ur_insts_impl(para_params, result);
     
     // 4. 生成浮点计算指令
     gen_float_compute_impl(para_params, result);
     
     return result;
 }

 
 std::vector<IqInstructionFile> SubOpInstructions::get_iq_files() const {
     std::vector<IqInstructionFile> files;
     
     auto serialize_header_bits = [](uint8_t vld, uint8_t jump, uint8_t embed, uint16_t loops,
                                     uint8_t cond, uint8_t dly, uint8_t inc,
                                     uint8_t embed_end_bits, uint8_t inc_embed_bits) -> uint64_t {
         uint64_t bits = 0;
         uint32_t pos = 0;
         uint8_t embed_end = (embed == 0) ? (embed_end_bits & 0x3Fu) : 0u;
         uint8_t inc_embed = (embed == 0) ? (inc_embed_bits & 0x3Fu) : 0u;
         bits |= (uint64_t)(inc_embed & 0x3Fu) << pos; pos += 6;
         bits |= (uint64_t)(inc & 0xFu) << pos; pos += 4;
         bits |= (uint64_t)(dly & 0x7u) << pos; pos += 3;
         bits |= (uint64_t)(cond & 0x7Fu) << pos; pos += 7;
         bits |= (uint64_t)(loops & 0x3FFu) << pos; pos += 10;
         bits |= (uint64_t)(embed_end & 0x3Fu) << pos; pos += 6;
         bits |= (uint64_t)(embed & 0x7u) << pos; pos += 3;
         bits |= (uint64_t)(jump & 1u) << pos; pos += 1;
         bits |= (uint64_t)(vld & 1u) << pos; pos += 1;
         return bits;
     };
 
     auto serialize_code = [](uint64_t code, uint16_t code_bits) -> std::vector<uint8_t> {
         std::vector<uint8_t> bytes;
         uint16_t num_bytes = (code_bits + 7) / 8;
         for (int i = num_bytes - 1; i >= 0; --i) bytes.push_back((code >> (i * 8)) & 0xFF);
         return bytes;
     };

     auto serialize_header = [&](uint8_t vld, uint8_t jump, uint8_t embed, uint16_t loops,
                                uint8_t cond, uint8_t dly, uint8_t inc,
                                uint8_t embed_end_bits, uint8_t inc_embed_bits) -> std::vector<uint8_t> {
         uint64_t bits = serialize_header_bits(vld, jump, embed, loops, cond, dly, inc, embed_end_bits, inc_embed_bits);
         std::vector<uint8_t> bytes;
         for (int i = 5; i >= 0; --i)
             bytes.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
         return bytes;
     };
     
     auto make_instruction = [&](uint8_t vld, uint8_t jump, uint8_t embed, uint16_t loops, 
                                 uint8_t cond, uint8_t dly, int8_t inc,
                                 uint8_t embed_end_bits, uint8_t inc_embed_bits,
                                 const std::vector<uint8_t>& code_bytes,
                                 int code_bits) -> std::vector<uint8_t> {
         if (code_bits > 0 && code_bits <= 23) {
              uint64_t h = serialize_header_bits(vld, jump, embed, loops, cond, dly, inc, embed_end_bits, inc_embed_bits);
              uint64_t c = 0;
              for (uint8_t b : code_bytes) {
                  c = (c << 8) | b;
              }
              if (code_bits < 64) {
                  c &= ((1ULL << code_bits) - 1);
              }
              uint64_t total = (h << code_bits) | c;
              int total_width = 41 + code_bits;
              int num_bytes = (total_width + 7) / 8;
              std::vector<uint8_t> ret;
              for (int i = num_bytes - 1; i >= 0; --i) {
                  ret.push_back(static_cast<uint8_t>((total >> (i * 8)) & 0xFF));
              }
              return ret;
         } else {
             auto header_bytes = serialize_header(vld, jump, embed, loops, cond, dly, inc, embed_end_bits, inc_embed_bits);
             header_bytes.insert(header_bytes.end(), code_bytes.begin(), code_bytes.end());
             return header_bytes;
         }
     };

      
     const SubSteadyStateSchedule schedule{};
     const uint16_t loops_inner = (para_total_steps >= 1) ? (para_total_steps - 1) : 0;
     const uint16_t loops_drain = loops_inner + SubSteadyStateSchedule::kPipelineDrain;
     const uint8_t inc_inner = 1u;
     
     // 1. UR-R0
     {
         IqInstructionFile file;
         file.para_iq_id = 14; file.para_iq_name = "ur-r0"; file.para_width_bits = 41 + 12;
         uint32_t code = extract_ur_read_port0_code(para_ur_read_crus[0]);
         auto code_bytes = serialize_code(code, 12);
         auto zero_code = serialize_code(0, 12);
         uint32_t total_dly = schedule.ur_r0;
         const uint8_t dly = emit_dummy_for_long_delay(total_dly, [&]() {
             auto nop = make_instruction(0, 0, 0, 7, 0, 0, 0, 0, 0, zero_code, 12);
             file.para_data.insert(file.para_data.end(), nop.begin(), nop.end());
         });
         auto inst = make_instruction(1, 0, 0u, loops_drain, 0, dly, inc_inner, 0u, 0u, code_bytes, 12);
         file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
         auto end_code = serialize_code(3ull << 40, 12);
         auto end_inst = make_instruction(0, 1, 0, 0, 0, 0, 0, 0u, 0u, end_code, 12);
         file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
         files.push_back(file);
     }
     
     // 2. UR-R1
     {
         IqInstructionFile file;
         file.para_iq_id = 15; file.para_iq_name = "ur-r1"; file.para_width_bits = 41 + 12;
         uint32_t code = extract_ur_read_port1_code(para_ur_read_crus[0]);
         auto code_bytes = serialize_code(code, 12);
         auto zero_code = serialize_code(0, 12);
         uint32_t total_dly = schedule.ur_r1;
         const uint8_t dly = emit_dummy_for_long_delay(total_dly, [&]() {
             auto nop = make_instruction(0, 0, 0, 7, 0, 0, 0, 0, 0, zero_code, 12);
             file.para_data.insert(file.para_data.end(), nop.begin(), nop.end());
         });
         auto inst = make_instruction(1, 0, 0u, loops_drain, 0, dly, inc_inner, 0u, 0u, code_bytes, 12);
         file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
         auto end_code = serialize_code(3ull << 40, 12);
         auto end_inst = make_instruction(0, 1, 0, 0, 0, 0, 0, 0u, 0u, end_code, 12);
         file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
         files.push_back(file);
     }
     
     // 3. UR-R2 (alpha, broadcast)
     {
         IqInstructionFile file;
         file.para_iq_id = 16; file.para_iq_name = "ur-r2"; file.para_width_bits = 41 + 12;
         uint32_t code = extract_ur_read_port2_code(para_ur_read_crus[0]);
         auto code_bytes = serialize_code(code, 12);
         auto zero_code = serialize_code(0, 12);
         uint32_t total_dly = schedule.ur_r2;
         const uint8_t dly = emit_dummy_for_long_delay(total_dly, [&]() {
             auto nop = make_instruction(0, 0, 0, 7, 0, 0, 0, 0, 0, zero_code, 12);
             file.para_data.insert(file.para_data.end(), nop.begin(), nop.end());
         });
         auto inst = make_instruction(1, 0, 0u, loops_drain, 0, dly, 0, 0u, 0u, code_bytes, 12);
         file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
         auto end_code = serialize_code(3ull << 40, 12);
         auto end_inst = make_instruction(0, 1, 0, 0, 0, 0, 0, 0u, 0u, end_code, 12);
         file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
         files.push_back(file);
     }
 
     // 4. RtoVR
     auto append_rtovr_inst = [&](uint8_t port, uint8_t select, uint32_t total_dly) {
         IqInstructionFile file;
         file.para_iq_id = 24 + port; file.para_iq_name = "rtovr-" + std::to_string(port); file.para_width_bits = 41 + 7;
         const uint8_t port_code = static_cast<uint8_t>(((select & 0x3Fu) << 1) | 1u);
         auto code_bytes = serialize_code(port_code, 7);
         auto zero_code = serialize_code(0, 7);
         const bool zero_dly_main = (port == RTOVR_OUT_UVR_WDATA_0);
         if (zero_dly_main) {
             consume_startup_delay(total_dly,
                 [&]() { auto nop = make_instruction(0, 0, 0, 0, 0, kMaxDlyField, 0, 0, 0, zero_code, 7); file.para_data.insert(file.para_data.end(), nop.begin(), nop.end()); },
                 [&](uint8_t precise_dly) { auto precise_nop = make_instruction(0, 0, 0, 0, 0, precise_dly, 0, 0, 0, zero_code, 7); file.para_data.insert(file.para_data.end(), precise_nop.begin(), precise_nop.end()); });
             auto inst = make_instruction(1, 0, 0u, loops_drain, 0, 0, 0, 0u, 0u, code_bytes, 7);
             file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
         } else {
             const uint8_t dly = emit_dummy_for_long_delay(total_dly, [&]() { auto nop = make_instruction(0, 0, 0, 0, 0, kMaxDlyField, 0, 0, 0, zero_code, 7); file.para_data.insert(file.para_data.end(), nop.begin(), nop.end()); });
             auto inst = make_instruction(1, 0, 0u, loops_drain, 0, dly, 0, 0u, 0u, code_bytes, 7);
             file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
         }
         auto end_code = serialize_code(3ull << 40, 7);
         auto end_inst = make_instruction(0, 1, 0, 0, 0, 0, 0, 0u, 0u, end_code, 7);
         file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
         files.push_back(file);
     };
 
     append_rtovr_inst(RTOVR_OUT_DVR_FPMUL_S0, RTOVR_IN_UR_RDATA_2, schedule.rtovr_fpmul_s0);
     append_rtovr_inst(RTOVR_OUT_DVR_FPMUL_S1, RTOVR_IN_UR_RDATA_1, schedule.rtovr_fpmul_s1);
     append_rtovr_inst(RTOVR_OUT_DVR_FPADD_S0, RTOVR_IN_UR_RDATA_0, schedule.rtovr_fpadd_s0);
     append_rtovr_inst(RTOVR_OUT_DVR_FPADD_S1, RTOVR_IN_DR_FPMUL_D, schedule.rtovr_fpadd_s1);
     append_rtovr_inst(RTOVR_OUT_UVR_WDATA_0, RTOVR_IN_DR_FPADD_D, schedule.rtovr_uwr0);
      
     // 5. FPMUL
     {
         IqInstructionFile file;
         file.para_iq_id = 5; file.para_iq_name = "fpmul"; file.para_width_bits = 41 + 3;
         auto code_bytes = serialize_code(para_fpmul_inst, 3);
         auto zero_code = serialize_code(0, 3);
         uint32_t total_dly = schedule.fpmul;
         const uint8_t dly = emit_dummy_for_long_delay(total_dly, [&]() {
             auto nop = make_instruction(0, 0, 0, 7, 0, 0, 0, 0, 0, zero_code, 3);
             file.para_data.insert(file.para_data.end(), nop.begin(), nop.end());
         });
         auto inst = make_instruction(1, 0, 0u, loops_drain, 0, dly, 0, 0u, 0u, code_bytes, 3);
         file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
         auto end_code = serialize_code(3ull << 40, 3);
         auto end_inst = make_instruction(0, 1, 0, 0, 0, 0, 0, 0u, 0u, end_code, 3);
         file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
         files.push_back(file);
     }
     
     // 6. FPADD
     {
         IqInstructionFile file;
         file.para_iq_id = 6; file.para_iq_name = "fpadd"; file.para_width_bits = 41 + 5;
         auto code_bytes = serialize_code(para_fpadd_inst, 5);
         auto zero_code = serialize_code(0, 5);
         uint32_t total_dly = schedule.fpadd;
         const uint8_t dly = emit_dummy_for_long_delay(total_dly, [&]() {
             auto nop = make_instruction(0, 0, 0, 7, 0, 0, 0, 0, 0, zero_code, 5);
             file.para_data.insert(file.para_data.end(), nop.begin(), nop.end());
         });
         auto inst = make_instruction(1, 0, 0u, loops_drain, 0, dly, 0, 0u, 0u, code_bytes, 5);
         file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
         auto end_code = serialize_code(3ull << 40, 5);
         auto end_inst = make_instruction(0, 1, 0, 0, 0, 0, 0, 0u, 0u, end_code, 5);
         file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
         files.push_back(file);
     }
     
     // 7. UR-W0
     {
         IqInstructionFile file;
         file.para_iq_id = 18; file.para_iq_name = "ur-w0"; file.para_width_bits = 41 + 28;
         auto serialize_ur_wdata_inst = [&](uint8_t vld, uint8_t jump, uint8_t embed, uint16_t loops,
                                            uint8_t cond, uint8_t dly, uint8_t inc,
                                            uint8_t embed_end_bits, uint8_t inc_embed_bits,
                                            uint32_t code_28) -> std::vector<uint8_t> {
             uint64_t header = serialize_header_bits(vld, jump, embed, loops, cond, dly, inc, embed_end_bits, inc_embed_bits);
             unsigned __int128 inst_69 = (static_cast<unsigned __int128>(header) << 28) | static_cast<unsigned __int128>(code_28 & 0x0FFFFFFFu);
             std::vector<uint8_t> bytes;
             for (int i = 8; i >= 0; --i) bytes.push_back(static_cast<uint8_t>((inst_69 >> (i * 8)) & 0xFF));
             return bytes;
         };
         uint32_t code = extract_ur_write_port0_code(para_ur_write_crus[0]);
         uint32_t total_dly = schedule.ur_w0;
         consume_startup_delay(total_dly,
             [&]() { auto dummy = serialize_ur_wdata_inst(1, 0, 0, 0, 0, kMaxDlyField, 0, 0, 0, 0); file.para_data.insert(file.para_data.end(), dummy.begin(), dummy.end()); },
             [&](uint8_t precise_dly) { auto precise_dummy = serialize_ur_wdata_inst(1, 0, 0, 0, 0, precise_dly, 0, 0, 0, 0); file.para_data.insert(file.para_data.end(), precise_dummy.begin(), precise_dummy.end()); });
         auto inst = serialize_ur_wdata_inst(1, 0, 0u, loops_inner, 0, 0, inc_inner, 0u, 0u, code);
         file.para_data.insert(file.para_data.end(), inst.begin(), inst.end());
         auto end_inst = serialize_ur_wdata_inst(0, 1, 0, 0, 0, 0, 0, 0u, 0u, 0);
         file.para_data.insert(file.para_data.end(), end_inst.begin(), end_inst.end());
         files.push_back(file);
     }
     
     return files;
 }
 
 std::string SubOp::get_name() const { return "FusedScaleSub"; }
 
 OpInstructionsBase* SubOp::compile(const app::AppOperatorRequest& req) {
     SubOpParams params;
     params.para_n = req.para_dimensions.para_n;
     params.para_c = req.para_dimensions.para_c;
     params.para_h = req.para_dimensions.para_h;
     params.para_w = req.para_dimensions.para_w;
     
     // 解析数据类型
     if (!req.para_inputs.empty()) {
         const auto& first_input = req.para_inputs.begin()->second;
         switch (first_input.para_dtype) {
             case app::AppDataType::FLOAT32: params.para_dtype = SubDataType::FP32; break;
             case app::AppDataType::FLOAT16: params.para_dtype = SubDataType::FP16; break;
             default: params.para_dtype = SubDataType::FP32; break;
         }
     }
     
     // UR 存储布局
     if (req.para_layout_mode == "bank_conflict") {
         const uint32_t total_elems = params.para_n * params.para_c * params.para_h * params.para_w;
         const uint32_t elems_per_word = params.get_elems_per_word();
         const uint32_t words_needed = (total_elems + elems_per_word - 1) / elems_per_word;
         
         int ib = (req.para_bc_input_bank  >= 0) ? req.para_bc_input_bank  : 0;
         int ob = (req.para_bc_other_bank  >= 0) ? req.para_bc_other_bank  : 0;
         int ab = (req.para_bc_alpha_bank  >= 0) ? req.para_bc_alpha_bank  : 0;
         int xb = (req.para_bc_output_bank >= 0) ? req.para_bc_output_bank : 1;
         
         params.para_input_bank  = ib;
         params.para_other_bank  = ob;
         params.para_alpha_bank  = ab;
         params.para_output_bank = xb;
         
         std::map<int, uint32_t> bank_next;
         params.para_input_addr  = bank_next[ib]; bank_next[ib] += words_needed;
         params.para_other_addr  = bank_next[ob]; bank_next[ob] += words_needed;
         params.para_alpha_addr  = bank_next[ab]; bank_next[ab] += 1;
         params.para_output_addr = bank_next[xb]; bank_next[xb] += words_needed;
     } else {
         params.para_input_bank = 0;  params.para_input_addr = 0;
         params.para_other_bank = 1;  params.para_other_addr = 0;
         params.para_alpha_bank = 2;  params.para_alpha_addr = 0;
         params.para_output_bank = 3; params.para_output_addr = 0;
     }

     SubOpInstructions* insts = new SubOpInstructions();
     *insts = compile(params);
     return insts;
 }

 
 bool SubOp::calculate_golden(const app::AppOperatorRequest& req, 
                             std::vector<uint8_t>& golden_bin) {
     golden_bin.clear();
     
     const auto& inputs = req.para_inputs;
     const auto& params = req.para_parameters;
     
     std::vector<float> input_f32, other_f32, alpha_f32;
     app::AppDataType dtype = app::AppDataType::FLOAT32;
     
     if (inputs.find("input_tensor") != inputs.end()) {
         dtype = inputs.at("input_tensor").para_dtype;
         input_f32 = inputs.at("input_tensor").para_data_f32;
     }
     if (inputs.find("other_tensor") != inputs.end()) {
         other_f32 = inputs.at("other_tensor").para_data_f32;
     }
     if (params.find("alpha") != params.end()) {
         alpha_f32 = params.at("alpha").para_data_f32;
     }
     
     const uint32_t w = req.para_dimensions.para_w;
     size_t n = std::min(input_f32.size(), other_f32.size());
     
     for (size_t i = 0; i < n; ++i) {
         float a = (!alpha_f32.empty() && w > 0) ? alpha_f32[i % w] : 1.0f;
         float out;
         
         if (dtype == app::AppDataType::FLOAT16) {
             double a_fp16 = static_cast<double>(app::float16_to_float32(app::float32_to_float16(a)));
             double other_fp16 = static_cast<double>(app::float16_to_float32(app::float32_to_float16(other_f32[i])));
             double input_fp16 = static_cast<double>(app::float16_to_float32(app::float32_to_float16(input_f32[i])));
             double mul_result_d = a_fp16 * other_fp16;
             double mul_fp16 = static_cast<double>(app::float16_to_float32(app::float32_to_float16(static_cast<float>(mul_result_d))));
             double sub_result_d = input_fp16 - mul_fp16;
             out = static_cast<float>(app::float16_to_float32(app::float32_to_float16(static_cast<float>(sub_result_d))));
         } else {
             out = input_f32[i] - a * other_f32[i];
         }
         
         if (dtype == app::AppDataType::FLOAT32) {
             uint32_t bits; memcpy(&bits, &out, sizeof(float));
             golden_bin.push_back(bits & 0xFF);
             golden_bin.push_back((bits >> 8) & 0xFF);
             golden_bin.push_back((bits >> 16) & 0xFF);
             golden_bin.push_back((bits >> 24) & 0xFF);
         } else {
             uint16_t bits = app::float32_to_float16(out);
             golden_bin.push_back(bits & 0xFF);
             golden_bin.push_back((bits >> 8) & 0xFF);
         }
     }
     
     return true;
 }

 void SubOp::fill_random_request(app::AppOperatorRequest& req, 
                                 std::mt19937& rng,
                                 std::uniform_real_distribution<float>& data_dist,
                                 const std::string& dtype_str) {
     req.para_operator = "FusedScaleSub";
     
     app::AppDataType target_type = app::AppDataType::FLOAT32;
     if (dtype_str == "fp16") target_type = app::AppDataType::FLOAT16;
     else if (dtype_str == "fp32") target_type = app::AppDataType::FLOAT32;

     uint32_t total = req.para_dimensions.para_n * req.para_dimensions.para_c * 
                      req.para_dimensions.para_h * req.para_dimensions.para_w;

     app::AppTensorDesc input_desc;
     input_desc.para_name = "input_tensor";
     input_desc.para_dtype = target_type;
     input_desc.para_shape = {req.para_dimensions.para_n, req.para_dimensions.para_c,
                              req.para_dimensions.para_h, req.para_dimensions.para_w};
     for (uint32_t i = 0; i < total; ++i) {
         input_desc.para_data_f32.push_back(data_dist(rng));
     }
     req.para_inputs["input_tensor"] = input_desc;

     app::AppTensorDesc other_desc = input_desc;
     other_desc.para_name = "other_tensor";
     other_desc.para_data_f32.clear();
     for (uint32_t i = 0; i < total; ++i) {
         other_desc.para_data_f32.push_back(data_dist(rng));
     }
     req.para_inputs["other_tensor"] = other_desc;

     app::AppTensorDesc alpha_desc;
     alpha_desc.para_name = "alpha";
     alpha_desc.para_dtype = target_type;
     alpha_desc.para_shape = {req.para_dimensions.para_w};
     std::uniform_real_distribution<float> alpha_dist(0.1f, 2.0f);
     for (uint32_t i = 0; i < req.para_dimensions.para_w; ++i) {
         alpha_desc.para_data_f32.push_back(alpha_dist(rng));
     }
     req.para_parameters["alpha"] = alpha_desc;
 }

 bool SubOp::generate_data(const app::AppOperatorRequest& req, 
                          std::vector<uint8_t>& input_bin, 
                          std::vector<uint8_t>& other_bin, 
                          std::vector<uint8_t>& alpha_bin) {
     input_bin.clear();
     other_bin.clear();
     alpha_bin.clear();
     
     auto write_tensor_data = [](std::vector<uint8_t>& bin, const app::AppTensorDesc& tensor) {
         if (tensor.para_dtype == app::AppDataType::FLOAT32) {
             for (float v : tensor.para_data_f32) {
                 uint32_t bits; memcpy(&bits, &v, sizeof(float));
                 bin.push_back(bits & 0xFF);
                 bin.push_back((bits >> 8) & 0xFF);
                 bin.push_back((bits >> 16) & 0xFF);
                 bin.push_back((bits >> 24) & 0xFF);
             }
         } else if (tensor.para_dtype == app::AppDataType::FLOAT16) {
             for (float v : tensor.para_data_f32) {
                 uint16_t bits = app::float32_to_float16(v);
                 bin.push_back(bits & 0xFF);
                 bin.push_back((bits >> 8) & 0xFF);
             }
         }
     };
     
     if (req.para_inputs.find("input_tensor") != req.para_inputs.end()) {
         write_tensor_data(input_bin, req.para_inputs.at("input_tensor"));
     }
     if (req.para_inputs.find("other_tensor") != req.para_inputs.end()) {
         write_tensor_data(other_bin, req.para_inputs.at("other_tensor"));
     }
     
     const auto& params = req.para_parameters;
     if (params.find("alpha") != params.end()) {
         write_tensor_data(alpha_bin, params.at("alpha"));
     }
     
     return true;
 }

 std::vector<uint8_t> SubOpInstructions::serialize() const {
     std::vector<uint8_t> result;
     auto iq_files = get_iq_files();
     for (const auto& file : iq_files) {
         result.insert(result.end(), file.para_data.begin(), file.para_data.end());
     }
     return result;
 }

 // 自动注册算子
 REGISTER_OP(SubOp);

 } // namespace op
 } // namespace aiacc

