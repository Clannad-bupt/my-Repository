#include "lsu_op_base.h"
#include "../iq/iq_id_defs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace aiacc {
namespace lsu {

uint16_t LsuOpBase::compose_ur_addr(uint8_t bank, uint16_t word_addr) {
    return static_cast<uint16_t>(((bank & 0x7u) << 12) | ((word_addr & 0xFFu) << 4));
}

uint16_t LsuOpBase::calc_ldb_loops(uint16_t word_count) const {
    return (word_count > 0) ? static_cast<uint16_t>(word_count - 1) : 0;
}

uint8_t LsuOpBase::calc_ldbaxi_loops(uint16_t word_count) const {
    uint32_t bursts = (word_count + 15) / 16;
    return static_cast<uint8_t>((bursts > 0) ? (bursts - 1) : 0);
}

std::vector<LsuOpBase::BurstSeg> LsuOpBase::bulk_burst_segments(uint16_t N) {
    std::vector<BurstSeg> segs;
    if (N == 0) return segs;
    uint16_t full = static_cast<uint16_t>(N / 16);
    uint16_t rem  = static_cast<uint16_t>(N % 16);
    if (full == 0) {                       // N in [1,15]：单段 N 拍
        segs.push_back({N, 1, 0});
        return segs;
    }
    uint64_t off = 0;
    if (rem == 1) {
        // 末尾 17 拍拆成 9+8，避免出现 brst=0（单拍 burst 会让 LDBAXI 卡住）
        if (full - 1 > 0) {
            segs.push_back({16, static_cast<uint16_t>(full - 1), 0});
            off = static_cast<uint64_t>(full - 1) * 256u;
        }
        segs.push_back({9, 1, off}); off += 9u * 16u;
        segs.push_back({8, 1, off});
    } else {
        segs.push_back({16, full, 0});
        off = static_cast<uint64_t>(full) * 256u;
        if (rem >= 2) segs.push_back({rem, 1, off});
    }
    return segs;
}

uint16_t LsuOpBase::largest_burst_beats(uint16_t word_count) {
    // 取 <=16 且能整除 word_count 的最大拍数，使 (拍数 × burst 数) == word_count，AXI 不多发
    if (word_count == 0) return 1;
    for (uint16_t b = (word_count < 16 ? word_count : 16); b >= 1; --b) {
        if (word_count % b == 0) return b;
    }
    return 1;
}

uint32_t LsuOpBase::total_input_words() const {
    uint32_t total = 0;
    for (auto& [name, port] : desc_.inputs) {
        total += port.word_count;
    }
    return total;
}

uint64_t LsuOpBase::input_gr_base(const std::string& name) const {
    auto it = input_gr_override_.find(name);
    if (it != input_gr_override_.end()) return it->second;

    // bulk 模式：HBM 数据连续存放，基址按 word_count*16 精确连续；
    // loops 模式：沿用 (word_count+1)*16 约定（与 loops 下 AXI 寻址对齐）。
    uint64_t offset = 0;
    for (auto& [n, port] : desc_.inputs) {
        if (n == name) return offset + gr_tile_offset_;
        offset += seg_stride_bytes(port.word_count);
    }
    return offset + gr_tile_offset_;
}

uint64_t LsuOpBase::store_gr_base() const {
    if (store_gr_override_valid_) return store_gr_override_;

    uint64_t offset = 0;
    for (auto& [name, port] : desc_.inputs) {
        offset += seg_stride_bytes(port.word_count);
    }
    return offset + gr_tile_offset_;
}

uint64_t LsuOpBase::seg_stride_bytes(uint16_t word_count) const {
    if (desc_.bulk_mode) {
        // bulk：每段按 4KB(256 行) 对齐，避免大 burst 跨 4KB 边界（AXI 非法跨界会回卷）
        uint32_t lines = ((static_cast<uint32_t>(word_count) + 255u) / 256u) * 256u;
        return static_cast<uint64_t>(lines) * 16u;
    }
    // loops：沿用 (word_count+1)*16 约定
    return static_cast<uint64_t>(word_count + 1u) * 16u;
}

op::LsuLoadParams LsuOpBase::build_load_params(uint8_t bank, uint16_t word_offset,
                                                uint16_t word_count, uint64_t gr_base) const {
    op::LsuLoadParams p{};
    p.para_ur_addr = compose_ur_addr(bank, word_offset);
    p.para_line_byte_cnt = 0x0F;            // 16 字节/行
    p.para_line_stride = 1;
    p.para_smc_id_en = desc_.smc_id_en;
    p.para_gr_base_addr = gr_base;
    p.para_gr_intlv_addr = desc_.gr_intlv_addr;
    p.para_ldb_vld = true;
    p.para_ldbaxi_vld = true;

    if (desc_.bulk_mode) {
        // LDB：一条指令写 word_count 行（ur_wr_line_cnt），不用外壳 loops
        p.para_ur_wr_line_cnt = static_cast<uint8_t>(word_count & 0xFF);
        p.para_ldb_loops = 0;
        p.para_ldb_inc = 0x0010;
        // LDBAXI 主体：16 拍/burst（brst=15），共 floor(N/16) 个 burst，地址每次 +0x100。
        // 余数 N%16 由 generate_load_iq_files 追加一条 LDBAXI 处理（见那里）。
        uint16_t full_bursts = static_cast<uint16_t>(word_count / 16);
        uint16_t rem = static_cast<uint16_t>(word_count % 16);
        if (full_bursts >= 1) {
            p.para_brst = 0x0F;                                   // 16 拍
            p.para_ldbaxi_loops = static_cast<uint16_t>(full_bursts - 1);
            p.para_ldbaxi_inc = 0x0100;                           // 每 burst +256B(16 行)
        } else {
            // N<16：主体即余数，一条 N 拍 burst
            p.para_brst = static_cast<uint8_t>((rem - 1) & 0x0F);
            p.para_ldbaxi_loops = 0;
            p.para_ldbaxi_inc = 0x0100;
        }
    } else {
        // loops 模式：每条 LDB 写 1 行，外壳 loops 重复 word_count 次
        p.para_ur_wr_line_cnt = 1;
        p.para_ldb_loops = calc_ldb_loops(word_count);
        p.para_ldbaxi_loops = calc_ldbaxi_loops(word_count);
        p.para_brst = 0x0F;
        p.para_ldb_inc = 0x0010;
        p.para_ldbaxi_inc = 0x0100;
    }
    return p;
}

op::LsuStoreParams LsuOpBase::build_store_params(uint8_t bank, uint16_t word_offset,
                                                  uint16_t word_count, uint64_t gr_base) const {
    op::LsuStoreParams p{};
    p.para_ur_addr = compose_ur_addr(bank, word_offset);
    p.para_line_byte_cnt = 0x0F;
    p.para_line_stride = 1;
    p.para_smc_id_en = desc_.smc_id_en;
    p.para_gr_base_addr = gr_base;
    p.para_gr_intlv_addr = desc_.store_gr_intlv_addr ? desc_.store_gr_intlv_addr
                                                     : desc_.gr_intlv_addr;
    p.para_stb_vld = true;
    p.para_stbaxi_vld = true;
    p.para_tid = 2;

    if (desc_.bulk_mode) {
        p.para_ur_rd_line_cnt = static_cast<uint8_t>(word_count & 0xFF);
        p.para_stb_loops = 0;
        p.para_stb_inc = 0x0010;
        uint16_t full_bursts = static_cast<uint16_t>(word_count / 16);
        uint16_t rem = static_cast<uint16_t>(word_count % 16);
        if (full_bursts >= 1) {
            p.para_brst = 0x0F;
            p.para_stbaxi_loops = static_cast<uint16_t>(full_bursts - 1);
            p.para_stbaxi_inc = 0x0100;
        } else {
            p.para_brst = static_cast<uint8_t>((rem - 1) & 0x0F);
            p.para_stbaxi_loops = 0;
            p.para_stbaxi_inc = 0x0100;
        }
    } else {
        p.para_ur_rd_line_cnt = 1;
        p.para_stb_loops = calc_ldb_loops(word_count);
        p.para_stbaxi_loops = calc_ldbaxi_loops(word_count);
        p.para_brst = 0x0F;
        p.para_stb_inc = 0x0010;
        p.para_stbaxi_inc = 0x0100;
    }
    return p;
}

std::vector<op::IqInstructionFile> LsuOpBase::generate_load_iq_files() const {
    std::vector<op::IqInstructionFile> all;
    for (auto& [name, port] : desc_.inputs) {
        uint64_t gr_base = input_gr_base(name);

        if (!desc_.bulk_mode) {
            auto p = build_load_params(port.bank, port.word_offset, port.word_count, gr_base);
            auto files = op::emit_load_iq_files(p);
            for (auto& f : files) all.push_back(std::move(f));
            continue;
        }
        // bulk：按 BULK_CHUNK_WORDS 拆分（word_count>255 时 8bit ur_wr_line_cnt 才会溢出，
        // 单条截断会让 LSU 挂死）；<=CHUNK 时退化为原来的一条 LDB + LDBAXI 分段。
        append_bulk_load(all, port.bank, port.word_offset, port.word_count, gr_base);
    }
    return all;
}

std::vector<op::IqInstructionFile> LsuOpBase::generate_store_iq_files() const {
    if (!desc_.bulk_mode) {
        auto p = build_store_params(desc_.output.bank, desc_.output.word_offset,
                                    desc_.output.word_count, store_gr_base());
        return op::emit_store_iq_files(p);
    }
    // bulk：同 load，走 BULK_CHUNK_WORDS 拆分，避免 word_count>255 时
    // 8bit ur_rd_line_cnt 截断；<=CHUNK 时与原先单条 STB + STBAXI 分段等价。
    std::vector<op::IqInstructionFile> all;
    append_bulk_store(all, desc_.output.bank, desc_.output.word_offset,
                      desc_.output.word_count, store_gr_base());
    return all;
}

void LsuOpBase::fill_input_port_data(const std::string& /*name*/,
                                     const PortInfo& port,
                                     uint8_t /*smc_id*/,
                                     std::mt19937& rng,
                                     std::vector<uint8_t>& out) const {
    // 跨数量级随机：尾数 [1,2) × 10^E（E∈[-20,20]）× 随机符号，仅有限值。
    // 范围约 ±1e-20 ~ ±2e20，覆盖正负与极大/极小，避免直接生成 inf/NaN/非规格化数。
    std::uniform_real_distribution<float> mant(1.0f, 2.0f);
    std::uniform_int_distribution<int> expo(-20, 20);
    std::uniform_int_distribution<int> sgn(0, 1);
    auto gen_val = [&]() -> float {
        float v = mant(rng) * std::pow(10.0f, static_cast<float>(expo(rng)));
        return sgn(rng) ? -v : v;
    };

    out.assign(static_cast<size_t>(port.word_count) * 16u, 0);
    for (uint32_t i = 0; i < static_cast<uint32_t>(out.size()) / 4; ++i) {
        float val = gen_val();
        uint32_t bits;
        std::memcpy(&bits, &val, 4);
        const uint64_t byte_off = static_cast<uint64_t>(i) * 4u;
        out[byte_off + 0] = static_cast<uint8_t>(bits & 0xFF);
        out[byte_off + 1] = static_cast<uint8_t>((bits >> 8) & 0xFF);
        out[byte_off + 2] = static_cast<uint8_t>((bits >> 16) & 0xFF);
        out[byte_off + 3] = static_cast<uint8_t>((bits >> 24) & 0xFF);
    }
}

void LsuOpBase::write_extra_meta_json(std::ostream& /*os*/) const {}

void LsuOpBase::generate_hbm_data(uint8_t smc_id, uint32_t seed,
                                   std::vector<uint8_t>& hbm_bytes) const {
    std::mt19937 rng(seed + smc_id * 1000);

    if (!desc_.bulk_mode) {
        // loops：所有输入连续存放（与 smc_0/golden 提取的连续布局一致；
        // gr_base 的 +1 只是寻址偏移，不是数据空隙）。
        uint32_t total_words = 0;
        for (auto& [name, port] : desc_.inputs) total_words += port.word_count;
        hbm_bytes.assign(static_cast<size_t>(total_words) * 16u, 0);
        uint64_t byte_offset = 0;
        for (auto& [name, port] : desc_.inputs) {
            std::vector<uint8_t> port_data;
            fill_input_port_data(name, port, smc_id, rng, port_data);
            const uint64_t end = byte_offset + port_data.size();
            if (end > hbm_bytes.size()) hbm_bytes.resize(static_cast<size_t>(end), 0);
            std::copy(port_data.begin(), port_data.end(), hbm_bytes.begin() + byte_offset);
            byte_offset += port_data.size();
        }
    } else {
        // bulk：每段放在 input_gr_base(name) 对齐基址，段间空隙补 0。
        uint64_t total = 0;
        for (auto& [name, port] : desc_.inputs)
            total = std::max(total, input_gr_base(name) + static_cast<uint64_t>(port.word_count) * 16);
        hbm_bytes.assign(static_cast<size_t>(total), 0);
        for (auto& [name, port] : desc_.inputs) {
            std::vector<uint8_t> port_data;
            fill_input_port_data(name, port, smc_id, rng, port_data);
            const uint64_t base = input_gr_base(name);
            const uint64_t end = base + port_data.size();
            if (end > hbm_bytes.size()) hbm_bytes.resize(static_cast<size_t>(end), 0);
            std::copy(port_data.begin(), port_data.end(), hbm_bytes.begin() + base);
        }
    }
}

static void mkdir_p(const std::string& path) {
    (void)system((std::string("mkdir -p \"") + path + "\"").c_str());
}

static void write_hex_file(const std::string& path, const std::vector<uint8_t>& data,
                           uint32_t line_width_bytes) {
    std::ofstream ofs(path);
    if (!ofs) return;
    uint32_t lines = static_cast<uint32_t>(data.size()) / line_width_bytes;
    for (uint32_t l = 0; l < lines; ++l) {
        for (int b = static_cast<int>(line_width_bytes) - 1; b >= 0; --b) {
            uint8_t byte = data[l * line_width_bytes + b];
            ofs << std::hex << std::setfill('0') << std::setw(2)
                << static_cast<unsigned>(byte);
        }
        ofs << "\n";
    }
}

static void write_zero_bank(const std::string& path, uint32_t lines) {
    std::ofstream ofs(path);
    if (!ofs) return;
    std::string zero_line(32, '0');
    for (uint32_t i = 0; i < lines; ++i) {
        ofs << zero_line << "\n";
    }
}

static std::string iq_filename_from_name(uint32_t /*id*/, const std::string& raw_name) {
    if (raw_name == "ldbaxi") return "ldb_axi";
    if (raw_name == "stbaxi") return "stb_axi";
    if (raw_name == "ur_rdata_0") return "ur-r0";
    if (raw_name == "ur_rdata_1") return "ur-r1";
    if (raw_name == "ur_rdata_2") return "ur-r2";
    if (raw_name == "ur_rdata_3") return "ur-r3";
    if (raw_name == "ur_wdata_0") return "ur-w0";
    if (raw_name == "ur_wdata_1") return "ur-w1";
    if (raw_name == "ur_wdata_2") return "ur-w2";
    if (raw_name == "ur_wdata_3") return "ur-w3";
    return raw_name;
}

// 把一组 IQ 文件按队列名合并后，写到指定目录下的 iq_<name>.hex（每条指令一行）。
// v0.4.20 multi_inst_stage：指令按 stage 分目录放在 case_root/smi_iq_inst/stage_<k>/，
// 是 smc_* 的同级（case 级），不再放进 smc_0/。
static void write_iq_files_to_dir(const std::string& dir,
                                  const std::vector<op::IqInstructionFile>& files) {
    struct IqFileEntry {
        std::vector<uint8_t> data;
        uint32_t width_bits{0};
    };
    std::map<std::string, IqFileEntry> file_map;
    for (auto& f : files) {
        std::string fname = iq_filename_from_name(f.para_iq_id, f.para_iq_name);
        auto& entry = file_map[fname];
        entry.data.insert(entry.data.end(), f.para_data.begin(), f.para_data.end());
        if (entry.width_bits == 0) entry.width_bits = f.para_width_bits;
    }
    for (auto& [fname, entry] : file_map) {
        std::string path = dir + "/iq_" + fname + ".hex";
        std::ofstream ofs(path);
        if (!ofs) continue;
        uint32_t inst_bytes = (entry.width_bits + 7) / 8;
        if (inst_bytes == 0) inst_bytes = static_cast<uint32_t>(entry.data.size());
        uint32_t num_insts = static_cast<uint32_t>(entry.data.size()) / inst_bytes;
        for (uint32_t n = 0; n < num_insts; ++n) {
            for (uint32_t b = 0; b < inst_bytes; ++b) {
                ofs << std::hex << std::setfill('0') << std::setw(2)
                    << static_cast<unsigned>(entry.data[n * inst_bytes + b]);
            }
            ofs << "\n";
        }
    }
}

bool LsuOpBase::assemble_full_case() const {
    const std::string case_root = cfg_.case_root;

    auto load_iqs = generate_load_iq_files();
    auto compute_iqs = generate_compute_iq_files();
    auto store_iqs = generate_store_iq_files();

    // ===== 4.20/4.21 multi-stage TB：按 stage 分目录写出 smi_iq_inst/stage_<k>/ =====
    // 不输出空中间 stage：TB 会等待每个非末尾 stage 拉起 done/intr，空 IQ 会超时。
    const std::string smi_iq_root = case_root + "/smi_iq_inst";
    (void)system((std::string("rm -rf \"") + smi_iq_root + "\"").c_str());
    mkdir_p(smi_iq_root);
    std::vector<std::pair<std::string, const std::vector<op::IqInstructionFile>*>> stages;
    stages.push_back({"load", &load_iqs});
    if (!compute_iqs.empty()) stages.push_back({"compute", &compute_iqs});
    stages.push_back({"store", &store_iqs});
    for (size_t i = 0; i < stages.size(); ++i) {
        const std::string stage_dir = smi_iq_root + "/stage_" + std::to_string(i);
        mkdir_p(stage_dir);
        write_iq_files_to_dir(stage_dir, *stages[i].second);
    }
    {
        std::ofstream sf(smi_iq_root + "/stages.md");
        if (sf) {
            sf << "# " << cfg_.case_name << " stages\n\n";
            sf << "- stage_count: " << stages.size() << "\n\n";
            for (size_t i = 0; i < stages.size(); ++i)
                sf << "- stage_" << i << ": " << stages[i].first << "\n";
        }
    }

    // ===== 4.19 三相触发版式：load+compute+store 指令合并写进 smc_0/iq_*.hex =====
    // （v3 的原生 bulk 发码不变，同时保留 4.19 兼容的 smc_0 合并输出，
    //  供 run_sim_aiacc_top_f.sh 消费；stage 分目录与 smc_0 合并输出并存。）

    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data");
        mkdir_p(smc_dir + "/output_bank");

        for (int b = 0; b < 8; ++b) {
            write_zero_bank(smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex", 256);
        }

        if (smc == 0) {
            struct IqFileEntry { std::vector<uint8_t> data; uint32_t width_bits{0}; };
            std::map<std::string, IqFileEntry> file_map;
            auto collect_iq = [&](const std::vector<op::IqInstructionFile>& files) {
                for (auto& f : files) {
                    std::string fname = iq_filename_from_name(f.para_iq_id, f.para_iq_name);
                    auto& entry = file_map[fname];
                    entry.data.insert(entry.data.end(), f.para_data.begin(), f.para_data.end());
                    if (entry.width_bits == 0) entry.width_bits = f.para_width_bits;
                }
            };
            collect_iq(load_iqs);
            collect_iq(compute_iqs);
            collect_iq(store_iqs);

            for (auto& [fname, entry] : file_map) {
                std::string path = smc_dir + "/iq_" + fname + ".hex";
                std::ofstream ofs(path);
                if (!ofs) continue;
                uint32_t inst_bytes = (entry.width_bits + 7) / 8;
                if (inst_bytes == 0) inst_bytes = static_cast<uint32_t>(entry.data.size());
                uint32_t num_insts = static_cast<uint32_t>(entry.data.size()) / inst_bytes;
                for (uint32_t n = 0; n < num_insts; ++n) {
                    for (uint32_t b = 0; b < inst_bytes; ++b)
                        ofs << std::hex << std::setfill('0') << std::setw(2)
                            << static_cast<unsigned>(entry.data[n * inst_bytes + b]);
                    ofs << "\n";
                }
            }
        }

        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);

        std::string hbm_path = cfg_.hbm_init_dir + "/memory_init" + std::to_string(smc + 1) + ".hex";
        write_hex_file(hbm_path, hbm_data, 16);

        // 按与 HBM 写入相同的布局提取各输入段（loops=连续 / bulk=对齐基址）
        std::map<std::string, std::vector<uint8_t>> input_datas;
        if (!desc_.bulk_mode) {
            uint32_t byte_offset = 0;
            for (auto& [name, port] : desc_.inputs) {
                uint32_t data_bytes = port.word_count * 16;
                input_datas[name] = std::vector<uint8_t>(
                    hbm_data.begin() + byte_offset,
                    hbm_data.begin() + byte_offset + data_bytes);
                byte_offset += data_bytes;
            }
        } else {
            for (auto& [name, port] : desc_.inputs) {
                uint64_t base = input_gr_base(name);
                uint32_t data_bytes = port.word_count * 16;
                input_datas[name] = std::vector<uint8_t>(
                    hbm_data.begin() + base,
                    hbm_data.begin() + base + data_bytes);
            }
        }

        std::vector<uint8_t> golden;
        generate_golden_for_smc(smc, input_datas, golden);

        const uint32_t ur_bank_depth = 256;
        if (golden.size() / 16 < ur_bank_depth) {
            golden.resize(ur_bank_depth * 16, 0);
        }

        write_hex_file(smc_dir + "/output_bank/bank" + std::to_string(desc_.output.bank) + "_final.hex",
                       golden, 16);
    }

    // ===== 落一份 meta_v3.json，供 DDR 真比对脚本(verify_ddr_v3.py)使用 =====
    {
        std::ofstream mf(case_root + "/meta_v3.json");
        if (mf) {
            mf << "{\n";
            mf << "  \"op\": \"" << get_name() << "\",\n";
            mf << "  \"num_smc\": " << static_cast<int>(desc_.num_smc) << ",\n";
            mf << "  \"out_bank\": " << static_cast<int>(desc_.output.bank) << ",\n";
            mf << "  \"out_words\": " << desc_.output.word_count << ",\n";
            mf << "  \"store_byte_base\": " << store_gr_base() << ",\n";
            mf << "  \"word_bytes\": 16,\n";
            write_extra_meta_json(mf);
            mf << "  \"inputs\": [\n";
            {
                size_t i = 0, ni = desc_.inputs.size();
                for (auto& [name, port] : desc_.inputs) {
                    mf << "    {\"name\": \"" << name << "\", \"byte_base\": " << input_gr_base(name)
                       << ", \"words\": " << port.word_count
                       << ", \"word_offset\": " << port.word_offset << "}"
                       << (++i < ni ? "," : "") << "\n";
                }
            }
            mf << "  ]\n";
            mf << "}\n";
        }
    }

    std::cout << "[LsuOpBase] Case assembled: " << case_root << " ("
              << static_cast<int>(desc_.num_smc) << " SMCs)" << std::endl;
    return true;
}

// ===== 多段版式(stage_0=Load / stage_1=Compute / stage_2=Store) =====
// 供 4.21 sim_multi_stage 中断流程消费;per-SMC 的 HBM/golden 与 4.19 版完全一致,
// 仅 IQ 写法不同(按 stage 分目录,不合并进 smc_0/iq_*.hex)。
bool LsuOpBase::assemble_full_case_multistage() const {
    const std::string case_root = cfg_.case_root;

    auto load_iqs  = generate_load_iq_files();
    auto store_iqs = generate_store_iq_files();

    std::string smi_root = case_root + "/smi_iq_inst";
    mkdir_p(smi_root + "/stage_0");
    mkdir_p(smi_root + "/stage_1");
    write_iq_files_to_dir(smi_root + "/stage_0", load_iqs);
    if (skip_compute_stage()) {
        // Load + Store only: avoid JUMP/compute placeholder executing real LDB.
        write_iq_files_to_dir(smi_root + "/stage_1", store_iqs);
    } else {
        auto compute_iqs = generate_compute_iq_files();
        mkdir_p(smi_root + "/stage_2");
        write_iq_files_to_dir(smi_root + "/stage_1", compute_iqs);
        write_iq_files_to_dir(smi_root + "/stage_2", store_iqs);
    }

    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data");
        mkdir_p(smc_dir + "/output_bank");
        for (int b = 0; b < 8; ++b)
            write_zero_bank(smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex", 256);

        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);
        write_hex_file(cfg_.hbm_init_dir + "/memory_init" + std::to_string(smc + 1) + ".hex", hbm_data, 16);

        std::map<std::string, std::vector<uint8_t>> input_datas;
        if (!desc_.bulk_mode) {
            uint32_t byte_offset = 0;
            for (auto& [name, port] : desc_.inputs) {
                uint32_t data_bytes = port.word_count * 16;
                input_datas[name] = std::vector<uint8_t>(hbm_data.begin() + byte_offset,
                                                         hbm_data.begin() + byte_offset + data_bytes);
                byte_offset += data_bytes;
            }
        } else {
            for (auto& [name, port] : desc_.inputs) {
                uint64_t base = input_gr_base(name);
                uint32_t data_bytes = port.word_count * 16;
                input_datas[name] = std::vector<uint8_t>(hbm_data.begin() + base,
                                                         hbm_data.begin() + base + data_bytes);
            }
        }

        std::vector<uint8_t> golden;
        generate_golden_for_smc(smc, input_datas, golden);
        const uint32_t ur_bank_depth = 256;
        if (golden.size() / 16 < ur_bank_depth) golden.resize(ur_bank_depth * 16, 0);
        write_hex_file(smc_dir + "/output_bank/bank" + std::to_string(desc_.output.bank) + "_final.hex",
                       golden, 16);
    }

    // ===== 落一份 meta_v3.json，供 DDR 真比对脚本(verify_ddr_v3.py)使用 =====
    {
        std::ofstream mf(case_root + "/meta_v3.json");
        if (mf) {
            mf << "{\n";
            mf << "  \"op\": \"" << get_name() << "\",\n";
            mf << "  \"num_smc\": " << static_cast<int>(desc_.num_smc) << ",\n";
            mf << "  \"out_bank\": " << static_cast<int>(desc_.output.bank) << ",\n";
            mf << "  \"out_words\": " << desc_.output.word_count << ",\n";
            mf << "  \"store_byte_base\": " << store_gr_base() << ",\n";
            mf << "  \"word_bytes\": 16,\n";
            mf << "  \"inputs\": [\n";
            {
                size_t i = 0, ni = desc_.inputs.size();
                for (auto& [name, port] : desc_.inputs) {
                    mf << "    {\"name\": \"" << name << "\", \"byte_base\": " << input_gr_base(name)
                       << ", \"words\": " << port.word_count
                       << ", \"word_offset\": " << port.word_offset << "}"
                       << (++i < ni ? "," : "") << "\n";
                }
            }
            mf << "  ]\n";
            mf << "}\n";
        }
    }

    std::cout << "[LsuOpBase] Multi-stage case assembled: " << case_root << " ("
              << static_cast<int>(desc_.num_smc) << " SMCs, stages="
              << (skip_compute_stage() ? 2 : 3) << ")" << std::endl;
    return true;
}

// ===== mm 专用 4.21 真 L-C-S（固定三阶段，不走 skip_compute）=====
// 与上面 assemble_full_case_multistage 独立：其他算子继续用后者。
bool LsuOpBase::assemble_multistage_case() const {
    const std::string case_root = cfg_.case_root;
    mkdir_p(case_root);

    auto load_iqs = generate_load_iq_files();
    auto compute_iqs = generate_compute_iq_files();
    auto store_iqs = generate_store_iq_files();

    // 4.21 真 L-C-S：与 mm 相同，三阶段分开，TB 等 smi_iq_done 再进下一 stage。
    const std::string smi_root = case_root + "/smi_iq_inst";
    mkdir_p(smi_root + "/stage_0");
    mkdir_p(smi_root + "/stage_1");
    mkdir_p(smi_root + "/stage_2");
    write_iq_files_to_dir(smi_root + "/stage_0", load_iqs);
    write_iq_files_to_dir(smi_root + "/stage_1", compute_iqs);
    write_iq_files_to_dir(smi_root + "/stage_2", store_iqs);

    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data");
        mkdir_p(smc_dir + "/output_bank");

        for (int b = 0; b < 8; ++b) {
            write_zero_bank(smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex", 256);
        }

        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);
        std::string hbm_path = cfg_.hbm_init_dir + "/memory_init" + std::to_string(smc + 1) + ".hex";
        write_hex_file(hbm_path, hbm_data, 16);

        std::map<std::string, std::vector<uint8_t>> input_datas;
        if (!desc_.bulk_mode) {
            uint32_t byte_offset = 0;
            for (auto& [name, port] : desc_.inputs) {
                uint32_t data_bytes = port.word_count * 16;
                input_datas[name] = std::vector<uint8_t>(
                    hbm_data.begin() + byte_offset,
                    hbm_data.begin() + byte_offset + data_bytes);
                byte_offset += data_bytes;
            }
        } else {
            for (auto& [name, port] : desc_.inputs) {
                uint64_t base = input_gr_base(name);
                uint32_t data_bytes = port.word_count * 16;
                input_datas[name] = std::vector<uint8_t>(
                    hbm_data.begin() + base,
                    hbm_data.begin() + base + data_bytes);
            }
        }

        std::vector<uint8_t> golden;
        generate_golden_for_smc(smc, input_datas, golden);
        if (golden.size() / 16 < 256u) {
            golden.resize(256u * 16u, 0);
        }
        write_hex_file(smc_dir + "/output_bank/bank" + std::to_string(desc_.output.bank) + "_final.hex",
                       golden, 16);
    }

    {
        std::ofstream mf(case_root + "/meta_v3.json");
        if (mf) {
            mf << "{\n";
            mf << "  \"op\": \"" << get_name() << "\",\n";
            mf << "  \"mode\": \"multistage\",\n";
            mf << "  \"num_smc\": " << static_cast<int>(desc_.num_smc) << ",\n";
            mf << "  \"out_bank\": " << static_cast<int>(desc_.output.bank) << ",\n";
            mf << "  \"out_words\": " << desc_.output.word_count << ",\n";
            mf << "  \"store_byte_base\": " << store_gr_base() << ",\n";
            mf << "  \"word_bytes\": 16,\n";
            mf << "  \"inputs\": [\n";
            {
                size_t i = 0, ni = desc_.inputs.size();
                for (auto& [name, port] : desc_.inputs) {
                    mf << "    {\"name\": \"" << name << "\", \"byte_base\": " << input_gr_base(name)
                       << ", \"words\": " << port.word_count
                       << ", \"word_offset\": " << port.word_offset << "}"
                       << (++i < ni ? "," : "") << "\n";
                }
            }
            mf << "  ]\n";
            mf << "}\n";
        }
    }

    std::cout << "[LsuOpBase] Multistage case assembled: " << case_root
              << " (stage_0=load, stage_1=compute, stage_2=store, "
              << static_cast<int>(desc_.num_smc) << " SMCs)" << std::endl;
    return true;
}

uint64_t LsuOpBase::tile_stride_bytes() const {
    // stride must ignore temporary multitile GR offset
    const uint64_t saved = gr_tile_offset_;
    gr_tile_offset_ = 0;
    const uint64_t stride =
        store_gr_base() + seg_stride_bytes(desc_.output.word_count);
    gr_tile_offset_ = saved;
    return stride;
}

// skip_compute 多 tile：每 tile 2 stage (Load/Store)。需 TB +TILE_STAGE_STRIDE=2。
bool LsuOpBase::assemble_full_case_multitile_ls(uint32_t K) const {
    if (!skip_compute_stage()) {
        std::cerr << "[LsuOpBase] multitile_ls requires skip_compute_stage()==true\n";
        return false;
    }
    const std::string case_root = cfg_.case_root;
    const uint64_t tile_stride = tile_stride_bytes();
    std::string smi_root = case_root + "/smi_iq_inst";

    for (uint32_t j = 0; j < K; ++j) {
        gr_tile_offset_ = static_cast<uint64_t>(j) * tile_stride;
        auto load_iqs = generate_load_iq_files();
        auto store_iqs = generate_store_iq_files();
        const std::string s_load =
            smi_root + "/stage_" + std::to_string(2 * j + 0);
        const std::string s_store =
            smi_root + "/stage_" + std::to_string(2 * j + 1);
        mkdir_p(s_load);
        mkdir_p(s_store);
        write_iq_files_to_dir(s_load, load_iqs);
        write_iq_files_to_dir(s_store, store_iqs);
    }
    gr_tile_offset_ = 0;

    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data");
        mkdir_p(smc_dir + "/output_bank");
        for (int b = 0; b < 8; ++b)
            write_zero_bank(smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex",
                            256);
        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);
        write_hex_file(cfg_.hbm_init_dir + "/memory_init" +
                           std::to_string(smc + 1) + ".hex",
                       hbm_data, 16);
        write_zero_bank(smc_dir + "/output_bank/bank" +
                            std::to_string(desc_.output.bank) + "_final.hex",
                        256);
    }

    std::cout << "[LsuOpBase] Multi-TILE L-S case: " << case_root << "  K=" << K
              << " stages=" << (2 * K) << " tile_stride=" << tile_stride << "B\n";
    return true;
}

// 大块 resident + L-S：DDR=[in0区|in1区|...|out区]，每区 M tile 紧排(word*16，无 4KB 空隙)。
bool LsuOpBase::assemble_full_case_resident_ls(uint32_t M) const {
    if (!skip_compute_stage()) {
        std::cerr << "[LsuOpBase] resident_ls requires skip_compute_stage()==true\n";
        return false;
    }
    if (M == 0) {
        std::cerr << "[LsuOpBase] resident_ls M==0\n";
        return false;
    }

    const std::string case_root = cfg_.case_root;
    std::string smi_root = case_root + "/smi_iq_inst";

    std::vector<std::string> in_names;
    std::vector<uint16_t> in_words;
    for (auto& [name, port] : desc_.inputs) {
        in_names.push_back(name);
        in_words.push_back(port.word_count);
    }
    const uint16_t out_words = desc_.output.word_count;

    std::vector<uint64_t> region_base(in_names.size(), 0);
    uint64_t cursor = 0;
    for (size_t i = 0; i < in_names.size(); ++i) {
        region_base[i] = cursor;
        cursor += static_cast<uint64_t>(M) * in_words[i] * 16u;
    }
    const uint64_t out_base = cursor;

    for (uint32_t j = 0; j < M; ++j) {
        input_gr_override_.clear();
        for (size_t i = 0; i < in_names.size(); ++i) {
            input_gr_override_[in_names[i]] =
                region_base[i] + static_cast<uint64_t>(j) * in_words[i] * 16u;
        }
        store_gr_override_valid_ = true;
        store_gr_override_ =
            out_base + static_cast<uint64_t>(j) * out_words * 16u;

        auto load_iqs = generate_load_iq_files();
        auto store_iqs = generate_store_iq_files();
        const std::string s_load =
            smi_root + "/stage_" + std::to_string(2 * j + 0);
        const std::string s_store =
            smi_root + "/stage_" + std::to_string(2 * j + 1);
        mkdir_p(s_load);
        mkdir_p(s_store);
        write_iq_files_to_dir(s_load, load_iqs);
        write_iq_files_to_dir(s_store, store_iqs);
    }
    input_gr_override_.clear();
    store_gr_override_valid_ = false;

    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data");
        mkdir_p(smc_dir + "/output_bank");
        for (int b = 0; b < 8; ++b)
            write_zero_bank(smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex",
                            256);
        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);
        write_hex_file(cfg_.hbm_init_dir + "/memory_init" +
                           std::to_string(smc + 1) + ".hex",
                       hbm_data, 16);
        write_zero_bank(smc_dir + "/output_bank/bank" +
                            std::to_string(desc_.output.bank) + "_final.hex",
                        256);
    }

    std::cout << "[LsuOpBase] Resident L-S case: " << case_root << "  M=" << M
              << " stages=" << (2 * M) << " out_base=" << out_base << "B\n";
    for (size_t i = 0; i < in_names.size(); ++i) {
        std::cout << "  region " << in_names[i] << " @ " << region_base[i]
                  << "  tile_bytes=" << (in_words[i] * 16u) << "\n";
    }
    std::cout << "  region output @ " << out_base
              << "  tile_bytes=" << (out_words * 16u) << "\n";
    return true;
}

// ===== 分块 load/store:word_count>255 时拆成同 bank 内多条(每条 <=CHUNK) =====
void LsuOpBase::append_bulk_load(std::vector<op::IqInstructionFile>& out,
                                 uint8_t bank, uint16_t word_offset,
                                 uint16_t word_count, uint64_t gr_base) const {
    for (uint16_t done = 0; done < word_count; done += BULK_CHUNK_WORDS) {
        uint16_t wc   = std::min<uint16_t>(BULK_CHUNK_WORDS, word_count - done);
        // 1 word 的 chunk（word_count==1 或 word_count%CHUNK==1）由下面
        // dma_wc=max(wc,16) pad 到 16 字发 burst，单拍段不会出现。
        uint16_t uoff = static_cast<uint16_t>(word_offset + done);
        uint64_t gr   = gr_base + static_cast<uint64_t>(done) * 16u;
        // The LSU's single-beat AXI burst does not retire.  Padding only
        // changes the DMA length; the logical tensor extent remains wc.
        const uint16_t dma_wc = std::max<uint16_t>(wc, 16);
        auto p = build_load_params(bank, uoff, dma_wc, gr);
        for (auto& f : op::emit_load_iq_files(p))
            if (f.para_iq_id == iq::IQ_ID_LDB) out.push_back(std::move(f));
        for (auto& seg : bulk_burst_segments(dma_wc)) {
            auto ps = p;
            ps.para_brst = static_cast<uint8_t>((seg.beats - 1) & 0x0F);
            ps.para_ldbaxi_loops = static_cast<uint16_t>(seg.num - 1);
            ps.para_ldbaxi_inc = 0x0100;
            ps.para_gr_base_addr = gr + seg.byte_off;
            for (auto& f : op::emit_load_iq_files(ps))
                if (f.para_iq_id == iq::IQ_ID_LDBAXI) out.push_back(std::move(f));
        }
    }
}

void LsuOpBase::append_bulk_store(std::vector<op::IqInstructionFile>& out,
                                  uint8_t bank, uint16_t word_offset,
                                  uint16_t word_count, uint64_t gr_base) const {
    for (uint16_t done = 0; done < word_count; done += BULK_CHUNK_WORDS) {
        uint16_t wc   = std::min<uint16_t>(BULK_CHUNK_WORDS, word_count - done);
        // 同 append_bulk_load：1 word 的 chunk 由 dma_wc pad 到 16 字，无单拍段。
        uint16_t uoff = static_cast<uint16_t>(word_offset + done);
        uint64_t gr   = gr_base + static_cast<uint64_t>(done) * 16u;

        const uint16_t dma_wc = std::max<uint16_t>(wc, 16);
        auto p = build_store_params(bank, uoff, dma_wc, gr);
        for (auto& f : op::emit_store_iq_files(p))
            if (f.para_iq_id == iq::IQ_ID_STB) out.push_back(std::move(f));
        for (auto& seg : bulk_burst_segments(dma_wc)) {
            auto ps = p;
            ps.para_brst = static_cast<uint8_t>((seg.beats - 1) & 0x0F);
            ps.para_stbaxi_loops = static_cast<uint16_t>(seg.num - 1);
            ps.para_stbaxi_inc = 0x0100;
            ps.para_gr_base_addr = gr + seg.byte_off;
            for (auto& f : op::emit_store_iq_files(ps))
                if (f.para_iq_id == iq::IQ_ID_STBAXI) out.push_back(std::move(f));
        }
    }
}

// ===== 驻留(连续)布局:一片 DDR = [input(M tile) | other(共享一份) | output(M tile)] =====
// tile j(0..M-1):
//   input  @ in_base  + j*(in_words*16)
//   other  @ oth_base + (j%other_cycle)*(other_seg_words*16)   <- 共享区,只 other_region_words
//   output @ out_base + j*(out_words*16)
// 其中 in_base=0, oth_base=M*in_words*16, out_base=oth_base+other_region_words*16。
bool LsuOpBase::assemble_full_case_resident(uint32_t M, uint32_t other_cycle,
                                            uint32_t other_region_words) const {
    const std::string case_root = cfg_.case_root;
    const uint16_t in_words  = desc_.inputs.at("input").word_count;
    const uint16_t oth_words = desc_.inputs.at("other").word_count;   // 单 tile 读的 other 段大小
    const uint16_t out_words = desc_.output.word_count;

    const uint64_t in_base  = 0;
    const uint64_t oth_base = static_cast<uint64_t>(M) * in_words * 16u;
    const uint64_t out_base = oth_base + static_cast<uint64_t>(other_region_words) * 16u;

    auto compute_iqs = generate_compute_iq_files();   // 每 tile 相同(UR 复用)

    std::string smi_root = case_root + "/smi_iq_inst";
    for (uint32_t j = 0; j < M; ++j) {
        // ---- Load: input(整段) + other(共享区第 j%other_cycle 段) ----
        std::vector<op::IqInstructionFile> load_iqs;
        {
            uint64_t gr_in = in_base + static_cast<uint64_t>(j) * in_words * 16u;
            append_bulk_load(load_iqs, desc_.inputs.at("input").bank,
                             desc_.inputs.at("input").word_offset, in_words, gr_in);
            uint64_t gr_oth = oth_base +
                static_cast<uint64_t>(j % other_cycle) * oth_words * 16u;
            append_bulk_load(load_iqs, desc_.inputs.at("other").bank,
                             desc_.inputs.at("other").word_offset, oth_words, gr_oth);
        }
        // ---- Store: output 段 ----
        std::vector<op::IqInstructionFile> store_iqs;
        {
            uint64_t gr_out = out_base + static_cast<uint64_t>(j) * out_words * 16u;
            append_bulk_store(store_iqs, desc_.output.bank,
                              desc_.output.word_offset, out_words, gr_out);
        }
        std::string s_load  = smi_root + "/stage_" + std::to_string(3*j+0);
        std::string s_comp  = smi_root + "/stage_" + std::to_string(3*j+1);
        std::string s_store = smi_root + "/stage_" + std::to_string(3*j+2);
        mkdir_p(s_load); mkdir_p(s_comp); mkdir_p(s_store);
        write_iq_files_to_dir(s_load, load_iqs);
        write_iq_files_to_dir(s_comp, compute_iqs);
        write_iq_files_to_dir(s_store, store_iqs);
    }

    // ---- 每 SMC: HBM(op 铺 input区+other区,output区留 0)+ 零 bank ----
    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data"); mkdir_p(smc_dir + "/output_bank");
        for (int b = 0; b < 8; ++b)
            write_zero_bank(smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex", 256);
        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);
        write_hex_file(cfg_.hbm_init_dir + "/memory_init" + std::to_string(smc + 1) + ".hex",
                       hbm_data, 16);
        write_zero_bank(smc_dir + "/output_bank/bank" + std::to_string(desc_.output.bank) + "_final.hex", 256);
    }

    std::cout << "[LsuOpBase] Resident case: " << case_root << "  M=" << M
              << " stages=" << (3*M) << " in_base=" << in_base
              << " oth_base=" << oth_base << " out_base=" << out_base
              << " (other_cycle=" << other_cycle << ", other_region=" << other_region_words
              << "w)\n";
    return true;
}

// 一次仿真 K 个 tile:3K 个 stage(tile j: stage 3j=Load / 3j+1=Compute / 3j+2=Store),
// 每 tile 的段 gr 偏移 = j*tile_stride。HBM 由 op 的 generate_hbm_data 按同布局铺 K 份。
bool LsuOpBase::assemble_full_case_multitile(uint32_t K) const {
    const std::string case_root = cfg_.case_root;
    const uint64_t tile_stride = tile_stride_bytes();
    auto compute_iqs = generate_compute_iq_files();   // UR 复用,每 tile 相同

    std::string smi_root = case_root + "/smi_iq_inst";
    // Repeated multi-tile LSU stages are more stable when a 128-word segment
    // is emitted as two 64-word transactions.  A single 128-word STB can leave
    // the LSU completion state stuck after a tile-boundary cfg_rst.
    constexpr uint16_t kMultitileChunkWords = 64;
    for (uint32_t j = 0; j < K; ++j) {
        uint64_t off = static_cast<uint64_t>(j) * tile_stride;

        // ---- load IQ (input/other,各加 tile 偏移) ----
        std::vector<op::IqInstructionFile> load_iqs;
        for (auto& [name, port] : desc_.inputs) {
            uint64_t gr_base = input_gr_base(name) + off;
            auto p = build_load_params(port.bank, port.word_offset, port.word_count, gr_base);
            if (!desc_.bulk_mode) {
                for (auto& f : op::emit_load_iq_files(p)) load_iqs.push_back(std::move(f));
            } else {
                for (uint16_t done = 0; done < port.word_count; done += kMultitileChunkWords) {
                    const uint16_t wc = std::min<uint16_t>(
                        kMultitileChunkWords, port.word_count - done);
                    append_bulk_load(load_iqs, port.bank,
                                     static_cast<uint16_t>(port.word_offset + done),
                                     wc, gr_base + static_cast<uint64_t>(done) * 16u);
                }
            }
        }
        // ---- store IQ(output 加 tile 偏移) ----
        std::vector<op::IqInstructionFile> store_iqs;
        {
            uint64_t gr_base = store_gr_base() + off;
            auto p = build_store_params(desc_.output.bank, desc_.output.word_offset,
                                        desc_.output.word_count, gr_base);
            if (!desc_.bulk_mode) {
                store_iqs = op::emit_store_iq_files(p);
            } else {
                for (uint16_t done = 0; done < desc_.output.word_count;
                     done += kMultitileChunkWords) {
                    const uint16_t wc = std::min<uint16_t>(
                        kMultitileChunkWords, desc_.output.word_count - done);
                    append_bulk_store(store_iqs, desc_.output.bank,
                                      static_cast<uint16_t>(desc_.output.word_offset + done),
                                      wc, gr_base + static_cast<uint64_t>(done) * 16u);
                }
            }
        }
        std::string s_load  = smi_root + "/stage_" + std::to_string(3*j+0);
        std::string s_comp  = smi_root + "/stage_" + std::to_string(3*j+1);
        std::string s_store = smi_root + "/stage_" + std::to_string(3*j+2);
        mkdir_p(s_load); mkdir_p(s_comp); mkdir_p(s_store);
        write_iq_files_to_dir(s_load, load_iqs);
        write_iq_files_to_dir(s_comp, compute_iqs);
        write_iq_files_to_dir(s_store, store_iqs);
    }

    // ---- 每 SMC: HBM(op 铺 K 份)+ 零 bank ----
    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data"); mkdir_p(smc_dir + "/output_bank");
        for (int b = 0; b < 8; ++b)
            write_zero_bank(smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex", 256);
        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);   // op 读 env(K)铺 K 份
        write_hex_file(cfg_.hbm_init_dir + "/memory_init" + std::to_string(smc + 1) + ".hex", hbm_data, 16);
        // 多 tile 验证从 HBM dump 读,output_bank golden 这里置零占位
        write_zero_bank(smc_dir + "/output_bank/bank" + std::to_string(desc_.output.bank) + "_final.hex", 256);
    }

    std::cout << "[LsuOpBase] Multi-TILE case: " << case_root << "  K=" << K
              << " stages=" << (3*K) << " tile_stride=" << tile_stride << "B\n";
    return true;
}

} // namespace lsu
} // namespace aiacc
