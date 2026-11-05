#include "matmul_nt_w4f16_lsu_op.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>

#include "../../app/fp16_utils.h"
#include "../../iq/iq_id_defs.h"
#include "../../op/common/lsu_emit.h"

namespace aiacc {
namespace lsu {

namespace {

void mkdir_p(const std::string& path) {
    (void)system((std::string("mkdir -p \"") + path + "\"").c_str());
}

void write_hex_file(const std::string& path, const std::vector<uint8_t>& data,
                    uint32_t line_width_bytes) {
    std::ofstream ofs(path);
    if (!ofs) return;
    const uint32_t lines = static_cast<uint32_t>(data.size()) / line_width_bytes;
    for (uint32_t l = 0; l < lines; ++l) {
        for (int b = static_cast<int>(line_width_bytes) - 1; b >= 0; --b) {
            const uint8_t byte = data[l * line_width_bytes + static_cast<uint32_t>(b)];
            ofs << std::hex << std::setfill('0') << std::setw(2)
                << static_cast<unsigned>(byte);
        }
        ofs << "\n";
    }
}

void write_zero_bank(const std::string& path, uint32_t lines) {
    std::ofstream ofs(path);
    if (!ofs) return;
    const std::string zero_line(32, '0');
    for (uint32_t i = 0; i < lines; ++i) {
        ofs << zero_line << "\n";
    }
}

void write_bank_with_prefix(const std::string& path, const std::vector<uint8_t>& prefix,
                            uint32_t total_lines) {
    std::vector<uint8_t> bank(total_lines * 16u, 0u);
    const size_t n = std::min(prefix.size(), bank.size());
    std::copy(prefix.begin(), prefix.begin() + static_cast<ptrdiff_t>(n), bank.begin());
    write_hex_file(path, bank, 16);
}

std::string iq_filename_from_name(uint32_t /*id*/, const std::string& raw_name) {
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

std::string iq_queue_key(const op::IqInstructionFile& f) {
    return std::to_string(f.para_iq_id) + ":" + std::to_string(f.para_width_bits);
}

void iq_strip_jump_end(op::IqInstructionFile& f) {
    const size_t inst_bytes = (static_cast<size_t>(f.para_width_bits) + 7u) / 8u;
    if (inst_bytes > 0 && f.para_data.size() >= inst_bytes) {
        f.para_data.resize(f.para_data.size() - inst_bytes);
    }
}

void iq_push_idle_file(std::vector<op::IqInstructionFile>& iqs, const op::IqInstructionFile& ref,
                       uint16_t cycles) {
    if (cycles == 0) return;
    op::IqInstructionFile idle = ref;
    idle.para_data = op::make_iq_idle_instruction(ref.para_width_bits, cycles);
    iqs.push_back(std::move(idle));
}

void write_iq_hex_files(const std::string& dir,
                        const std::vector<op::IqInstructionFile>& load_iqs,
                        const std::vector<op::IqInstructionFile>& compute_iqs,
                        const std::vector<op::IqInstructionFile>& store_iqs = {}) {
    struct IqFileEntry {
        std::vector<uint8_t> data;
        uint32_t width_bits{0};
    };
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

    mkdir_p(dir);
    for (auto& [fname, entry] : file_map) {
        std::string path = dir + "/iq_" + fname + ".hex";
        std::ofstream ofs(path);
        if (!ofs) continue;
        const uint32_t inst_bytes = (entry.width_bits + 7) / 8;
        const uint32_t num_insts = static_cast<uint32_t>(entry.data.size()) / inst_bytes;
        for (uint32_t n = 0; n < num_insts; ++n) {
            for (uint32_t b = 0; b < inst_bytes; ++b) {
                ofs << std::hex << std::setfill('0') << std::setw(2)
                    << static_cast<unsigned>(entry.data[n * inst_bytes + b]);
            }
            ofs << "\n";
        }
    }
}

}  // namespace

namespace {

bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    ifs.seekg(0, std::ios::end);
    const std::streamoff sz = ifs.tellg();
    if (sz <= 0) return false;
    ifs.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(sz));
    ifs.read(reinterpret_cast<char*>(out.data()), sz);
    return static_cast<bool>(ifs);
}

}  // namespace

uint32_t MatmulNtW4f16LsuOp::m_rows() const {
    if (const char* e = std::getenv("MM_M")) {
        const int v = std::atoi(e);
        if (v == 1 || v == 8 || v == static_cast<int>(op::kMatmulNtW4f16M256))
            return static_cast<uint32_t>(v);
    }
    return 8u;
}

bool MatmulNtW4f16LsuOp::is_fp16() const {
    // MatMul_NT_w4f16：GPTQ 算子，恒 false（不读 MM_QUANT，与 op 侧 parse_request 同口径）。
    return false;
}

bool MatmulNtW4f16LsuOp::out_f16() const {
    if (is_m256()) return true;   // D4：prefill 恒 f16 packed 输出
    // M=1（D8 退化 epilogue，Phase 6）：同 M=8 由 MM_OUT_DTYPE=f16 打开；仅 multistage
    // 路径可用（扁平路径无 epilogue，assemble_full_case 入口对此 throw）。
    if (const char* e = std::getenv("MM_OUT_DTYPE")) return std::string(e) == "f16";
    return false;
}

uint32_t MatmulNtW4f16LsuOp::k_chunk() const {
    return is_m1() ? op::kMatmulNtW4f16M1KTile : op::kMatmulNtW4f16M8KChunk;
}

uint32_t MatmulNtW4f16LsuOp::a_words_per_smc() const {
    // M=256：A tile = 256 行 × 32 K × 2B = 16KB / 8 SMC = 128 word/SMC（bank0 恰可放 k_batch=2）。
    if (is_m256()) return 128u;
    // M=1/M=8：2-word 间插后均为 4 word/SMC（M=1 靠 shiftup 广播补齐）。
    return 4u;
}

uint32_t MatmulNtW4f16LsuOp::b_words_per_smc() const {
    if (is_fp16()) {
        // fp16×fp16 v1：仅 M=8；32×32 fp16 → 128B×32 / 8 SMC = 16 word/SMC。
        if (is_m1()) {
            throw std::runtime_error("MatmulNtW4f16LsuOp: MM_QUANT=fp16 does not support M=1 yet");
        }
        return 16u;
    }
    // M=1：256×32 int4 → 256 个 128b word / 8 SMC = 32 word/SMC。
    return is_m1() ? 32u : 4u;
}

uint32_t MatmulNtW4f16LsuOp::out_words() const {
    // f16 packed 输出：bank6 每 word = 行对 × 4 列 → M/2 word（M=256→128，M=8+f16→4）；
    // M=1（D8）保持 2 word（1 有效 + 1 padding，单拍 STBAXI 会挂死）。
    if (out_f16()) return is_m1() ? 2u : m_rows() / 2u;
    // M=1 逻辑输出 1 word，但 bulk STBAXI 不支持单拍 burst（brst=0 卡死 AXI，见
    // append_bulk_store 的 guard）。按 guard 建议把 store 垫到 2 word：bank7 word1
    // 恒为 0（compute 只写 word0，golden 零填充），比对结果不受影响。
    return is_m1() ? 2u : 8u;
}

void MatmulNtW4f16LsuOp::ensure_external_loaded() const {
    if (ext_.attempted) return;
    ext_.attempted = true;
    const char* dir_env = std::getenv("MM_TENSOR_DIR");
    if (!dir_env || dir_env[0] == '\0') return;

    const std::string dir(dir_env);
    const uint32_t kTot = k_total();
    const size_t kARows = static_cast<size_t>(m_rows());
    const size_t kNCols = static_cast<size_t>(n_total());   // B/zp/scale 文件宽（MM_N）

    std::vector<uint8_t> raw_a;
    std::vector<uint8_t> raw_bq;
    std::vector<uint8_t> raw_zp;
    std::vector<uint8_t> raw_sc;
    const std::string a_pref =
        is_m1() ? "a_1x" : (is_m256() ? "a_256x" : "a_8x");
    if (!read_file_bytes(dir + "/" + a_pref + "4096_f32.bin", raw_a) &&
        !read_file_bytes(dir + "/" + a_pref + std::to_string(kTot) + "_f32.bin", raw_a) &&
        !(is_m1() && read_file_bytes(dir + "/a_8x" + std::to_string(kTot) + "_f32.bin", raw_a))) {
        std::cerr << "[MatmulNtW4f16LsuOp] WARN: MM_TENSOR_DIR set but A bin missing under " << dir
                  << std::endl;
        return;
    }
    // M=1 若误读到 8×K 文件，只取第一行。
    size_t expect_a = kARows * kTot * sizeof(float);
    if (is_m1() && raw_a.size() == 8u * kTot * sizeof(float)) {
        expect_a = 8u * kTot * sizeof(float);
    }
    if (raw_a.size() != expect_a && raw_a.size() != kARows * kTot * sizeof(float)) {
        std::cerr << "[MatmulNtW4f16LsuOp] WARN: A size mismatch got=" << raw_a.size()
                  << " expect=" << (kARows * kTot * sizeof(float)) << std::endl;
        return;
    }
    if (is_m1() && raw_a.size() == 8u * kTot * sizeof(float)) {
        ext_.a.resize(kTot);
        std::memcpy(ext_.a.data(), raw_a.data(), kTot * sizeof(float));
    } else {
        ext_.a.resize(kARows * kTot);
        std::memcpy(ext_.a.data(), raw_a.data(), kARows * kTot * sizeof(float));
    }

    // fp16×fp16：稠密 B 为 K×64 f32；无 zp/scale。
    if (is_fp16()) {
        std::vector<uint8_t> raw_b;
        const std::string b_name =
            dir + "/b_" + std::to_string(kTot) + "x" + std::to_string(kNCols) + "_f32.bin";
        if (!read_file_bytes(b_name, raw_b)) {
            std::cerr << "[MatmulNtW4f16LsuOp] WARN: missing " << b_name << std::endl;
            return;
        }
        const size_t expect_b = static_cast<size_t>(kTot) * kNCols * sizeof(float);
        if (raw_b.size() != expect_b) {
            std::cerr << "[MatmulNtW4f16LsuOp] WARN: B fp32 size mismatch got=" << raw_b.size()
                      << " expect=" << expect_b << std::endl;
            return;
        }
        ext_.b_fp32.resize(static_cast<size_t>(kTot) * kNCols);
        std::memcpy(ext_.b_fp32.data(), raw_b.data(), expect_b);
        ext_.bq.clear();
        ext_.zp.clear();
        ext_.scale.clear();
        ext_.group_size = 1u;
        ext_.num_groups = 1u;
        ext_.valid = true;
        std::cout << "[MatmulNtW4f16LsuOp] Loaded external fp16 tensors from " << dir << " (K=" << kTot
                  << ", Npack=64)" << std::endl;
        return;
    }

    const std::string bq_name =
        dir + "/bq_" + std::to_string(kTot) + "x" + std::to_string(kNCols) + "_u4.bin";
    if (!read_file_bytes(bq_name, raw_bq)) {
        std::cerr << "[MatmulNtW4f16LsuOp] WARN: missing " << bq_name << std::endl;
        return;
    }
    const size_t expect_bq = static_cast<size_t>(kTot) * kNCols;
    if (raw_bq.size() != expect_bq) {
        std::cerr << "[MatmulNtW4f16LsuOp] WARN: Bq size mismatch" << std::endl;
        return;
    }
    ext_.bq = std::move(raw_bq);

    if (const char* gs = std::getenv("MM_GROUP_SIZE")) {
        const int v = std::atoi(gs);
        if (v > 0) ext_.group_size = static_cast<uint32_t>(v);
    }
    if (kTot % ext_.group_size != 0) {
        std::cerr << "[MatmulNtW4f16LsuOp] WARN: K_TOTAL not divisible by group_size" << std::endl;
        return;
    }
    ext_.num_groups = kTot / ext_.group_size;

    const std::string zp_name = dir + "/zp_" + std::to_string(ext_.num_groups) + "x" +
                                std::to_string(kNCols) + "_u4.bin";
    const std::string sc_name = dir + "/scale_" + std::to_string(ext_.num_groups) + "x" +
                                std::to_string(kNCols) + "_f16.bin";
    if (!read_file_bytes(zp_name, raw_zp) || !read_file_bytes(sc_name, raw_sc)) {
        std::cerr << "[MatmulNtW4f16LsuOp] WARN: missing zp/scale under " << dir << std::endl;
        return;
    }
    const size_t expect_zp = static_cast<size_t>(ext_.num_groups) * kNCols;
    const size_t expect_sc = expect_zp * sizeof(uint16_t);
    if (raw_zp.size() != expect_zp || raw_sc.size() != expect_sc) {
        std::cerr << "[MatmulNtW4f16LsuOp] WARN: zp/scale size mismatch" << std::endl;
        return;
    }
    ext_.zp = std::move(raw_zp);
    ext_.scale.assign(expect_zp, 0.0f);
    for (size_t i = 0; i < expect_zp; ++i) {
        uint16_t h = static_cast<uint16_t>(raw_sc[i * 2 + 0]) |
                     (static_cast<uint16_t>(raw_sc[i * 2 + 1]) << 8);
        ext_.scale[i] = app::float16_to_float32(h);
    }
    ext_.valid = true;
    std::cout << "[MatmulNtW4f16LsuOp] Loaded external tensors from " << dir << " (K=" << kTot
              << ", groups=" << ext_.num_groups << ", group_size=" << ext_.group_size << ")"
              << std::endl;
}

LsuComputeDesc MatmulNtW4f16LsuOp::default_compute_desc() {
    LsuComputeDesc d;
    // 静态入口：按 MM_M / MM_QUANT 选 word 数（与实例方法口径一致）。
    uint32_t m = 8u;
    if (const char* e = std::getenv("MM_M")) {
        const int v = std::atoi(e);
        if (v == 1 || v == 8 || v == static_cast<int>(op::kMatmulNtW4f16M256))
            m = static_cast<uint32_t>(v);
    }
    constexpr bool fp16 = false;  // MatMul_NT_w4f16 固定 GPTQ（不读 MM_QUANT）。
    if (fp16 && m == 1u) {
        throw std::runtime_error("MatmulNtW4f16LsuOp::default_compute_desc: fp16 + M=1 not supported yet");
    }
    // M=256：A=128 word/SMC/块（bank0 恰放 k_batch=2）。
    const uint16_t a_w = static_cast<uint16_t>(m == op::kMatmulNtW4f16M256 ? 128u : 4u);
    const uint16_t b_w = fp16 ? 16u : static_cast<uint16_t>(m == 1u ? 32u : 4u);
    // M=1 垫 2 word：避免单拍 STBAXI；f16 输出：bank6 的 M/2 个 packed f16 word
    // （M=256 恒 f16；M=8/M=1 由 MM_OUT_DTYPE=f16 打开——M=8 用于阶梯 1 快验，M=1 为
    //  Phase 6 退化 epilogue，仅 multistage 路径）。
    const bool f16_out =
        (m == op::kMatmulNtW4f16M256) ||
        ((m == 8u || m == 1u) && std::getenv("MM_OUT_DTYPE") != nullptr &&
         std::string(std::getenv("MM_OUT_DTYPE")) == "f16");
    const uint16_t out_w = static_cast<uint16_t>(
        m == 1u ? 2u : (f16_out ? m / 2u : 8u));
    d.inputs["a_tensor"] = {0, 0, a_w};
    d.inputs["b_tensor"] = {1, 0, b_w};
    if (!fp16) {
        // GPTQ：zp→bank4、scale→bank5。fp16 模式不载 zp/scale。
        const uint16_t zs_w = static_cast<uint16_t>(m == 1u ? 16u : 4u);
        d.inputs["zp_tensor"] = {4, 0, zs_w};
        d.inputs["scale_tensor"] = {5, 0, zs_w};
    }
    // f16 输出：epilogue 落 bank6，store 从 bank6 读。
    d.output = {static_cast<uint8_t>(f16_out ? 6u : 7u), 0, out_w};
    d.num_smc = 8;
    d.smc_id_en = 7;   // 最后一个 SMC 的下标(0基)=num_smc-1；LDB/STB 的 crd 完成信号只在 smc_id==smc_id_en 生成，误设为 8 会导致完成永不产生→load/store 卡死
    // f16 输出（D5 store 拼接）：拼接语义 = 8 SMC 各 2-word chunk 按 burst 间 256B 步进
    // 拼成连续 256B 块（intlv=0x20）。但阶梯 2（M=256, 64×32B burst/SMC）实测：TB 把
    // intlv=0x20 的全部地址路由到同一个 1MB SRAM 通道，8 SMC 持续流挤一个口时 STBAXI
    // 64B 环形 buffer 读出整环（64B=4 word）前的陈旧数据（smc_1..7 大面积错，波形实证
    // 流入 STBAXI 的 128 beat 全对、错误出现在自身 AXI W 口）——RTL 冻结，软件规避：
    // RTL 验证默认 store intlv=0x10000000（每 SMC 独占通道，与历史所有 PASS 算子同拓扑），
    // 拼接布局由 verify_ddr_v3 按地址重建；FPGA 拼接用 MM_STORE_INTLV=0x20 显式打开
    // （intlv=0x20 的拼接地址算术本身已由阶梯 1（M=8, 2 rounds, 8 SMC）端到端验证）。
    // load 侧 RTL 保持历史 0x10000000（LDB 也用 gr_base+smc*intlv）。
    d.gr_intlv_addr = 0x10000000ULL;
    d.store_gr_intlv_addr = 0u;
    if (f16_out) {
        uint64_t intlv = 0x10000000ULL;   // 默认 per-channel（RTL 安全）
        if (const char* e = std::getenv("MM_STORE_INTLV")) {
            intlv = std::strtoull(e, nullptr, 0);   // 支持 0x20 等显式值（FPGA 拼接）
        } else if (slot_map_fpga()) {
            intlv = 0x20ULL;   // D7：FPGA 槽位图隐式拼接布局
        }
        d.store_gr_intlv_addr = intlv;
    }
    if (slot_map_fpga()) {
        // FPGA 固定全局图（2026-09-30）：load intlv 从"窗跨步"重定义为"切片跨步" S。
        // 每输入全 HBM 单拷贝，8 个 SMC 切片按统一步长 S 打包在槽内：
        //   SMC s 的块 kb 落地址 = slot_base + s*S + kb*chunk（硬件 gr_base + smc*intlv）
        //   → S 取所有输入切片字节数的最大值（短切片补零垫到 S）。
        // 页安全：各输入 chunk 均为 2 的幂且整除 S，burst 地址
        // chunk 对齐、burst ≤ min(chunk,256B) ⇒ 永不跨 4KB 页（同 region_stride_bytes 不变量）。
        // 常量在此按 env 现算（静态入口无 this）：
        const uint32_t chunk = (m == 1u) ? op::kMatmulNtW4f16M1KTile : op::kMatmulNtW4f16M8KChunk;
        uint32_t kt = (m == 1u) ? op::kMatmulNtW4f16M1KTile : 64u;
        if (const char* e = std::getenv("K_TOTAL")) {
            const int v = std::atoi(e);
            if (v >= static_cast<int>(chunk) && (v % static_cast<int>(chunk)) == 0)
                kt = static_cast<uint32_t>(v);
        }
        const uint32_t nkb = kt / chunk;
        uint64_t s = 0;
        for (auto& [name, port] : d.inputs) {
            (void)name;
            s = std::max(s, static_cast<uint64_t>(nkb) * port.word_count * 16u);
        }
        // S 向上取 2 的幂：《AIACC_U280验证总览》§5.4/§7 要求 chunk（=load/store intlv）
        // 为 2 的幂——板上实测非 2 幂 chunk 出现 "smc 偶数过奇数不过"。K=11008 时
        // nkb=43(M=1)/344(M=256)：S=43×512=22016 / 43×2^14=704512 均非 2 幂
        // → 32768 / 1048576。SMC 侧 smc×intlv 是移位加精确乘法（ldb/stb_ur_axi_conv.v），
        // 非 2 幂在 DUT 内其实正确，但 FPGA 地址通路（BMC→HBM 交织）不在本仓可见，
        // pow2 是文档要求且零风险（槽距 256MB，补零可忽略；S 仍为各 chunk 的整数倍，
        // 页安全不变量不受影响）。
        uint64_t s_pow2 = 16u;
        while (s_pow2 < s) s_pow2 <<= 1;
        d.gr_intlv_addr = s_pow2;
        if (d.store_gr_intlv_addr == 0u) {
            // fp32 输出 + FPGA 槽位图：store 同样走 0x20 拼接（每 SMC 2-word chunk 交错
            // 拼 256B 块，与 f16 的 D5 布局同构，只是 payload 是 fp32 word0 + 零垫 word1）。
            // runner stitched 比对按 meta gr_intlv_addr==0x20 dispatch，此处必须固定 0x20。
            d.store_gr_intlv_addr = 0x20ULL;
        }
    }
    d.bulk_mode = true;
    return d;
}

uint32_t MatmulNtW4f16LsuOp::k_total() const {
    const uint32_t chunk = k_chunk();
    if (const char* e = std::getenv("K_TOTAL")) {
        const int v = std::atoi(e);
        if (v >= static_cast<int>(chunk) && (v % static_cast<int>(chunk)) == 0) {
            return static_cast<uint32_t>(v);
        }
    }
    return is_m1() ? op::kMatmulNtW4f16M1KTile : 64u;
}

uint32_t MatmulNtW4f16LsuOp::num_k_blocks() const { return k_total() / k_chunk(); }

uint32_t MatmulNtW4f16LsuOp::n_total() const {
    uint32_t n = 64u;
    if (const char* e = std::getenv("MM_N")) {
        const int v = std::atoi(e);
        if (v >= 32 && v <= 65535) n = static_cast<uint32_t>(v);
    }
    return n;
}

uint32_t MatmulNtW4f16LsuOp::n_tile_idx() const {
    uint32_t t = 0u;
    if (const char* e = std::getenv("MM_N_TILE_START")) {
        const int v = std::atoi(e);
        if (v >= 0 && v <= 2047) t = static_cast<uint32_t>(v);
    }
    return t;
}

uint32_t MatmulNtW4f16LsuOp::col_base() const { return n_tile_idx() * 32u; }

bool MatmulNtW4f16LsuOp::slot_map_fpga() {
    const char* e = std::getenv("MM_SLOT_MAP");
    return e && std::strcmp(e, "fpga") == 0;
}

uint32_t MatmulNtW4f16LsuOp::quant_group_size() const {
    ensure_external_loaded();
    return ext_.valid ? ext_.group_size : 64u;
}

uint32_t MatmulNtW4f16LsuOp::k_batch_size() const {
    // M=1 默认 1：单块已占 A/B 各 32 word，先稳 bring-up。
    // M=256 默认 2：A=128 word/块，bank0 恰放两块（上限约束同样收敛到 2）。
    uint32_t batch = is_m1() ? 1u : (is_m256() ? 2u : 4u);
    if (const char* e = std::getenv("MM_K_BATCH")) {
        const int v = std::atoi(e);
        if (v >= 1 && v <= 64) batch = static_cast<uint32_t>(v);
    }
    constexpr uint32_t kUrBankWords = 256u;
    const uint32_t aw = std::max(1u, a_words_per_smc());
    const uint32_t bw = std::max(1u, b_words_per_smc());
    // A/B 各自连续占槽，batch 上限取两者更紧的约束（fp16：bw=16 → max 16）。
    uint32_t max_batch = std::min(kUrBankWords / aw, kUrBankWords / bw);
    if (!is_fp16()) {
        const uint32_t max_by_zs = kUrBankWords / kZpWordsPerSmc;
        max_batch = std::min(max_batch, max_by_zs);
    }
    if (batch > max_batch) {
        static bool warned = false;
        if (!warned) {
            std::cerr << "[MatmulNtW4f16LsuOp] MM_K_BATCH=" << batch << " clamped to " << max_batch
                      << " (UR bank 256-word 限制)" << std::endl;
            warned = true;
        }
        batch = max_batch;
    }
    return batch;
}

uint64_t MatmulNtW4f16LsuOp::region_stride_bytes(uint16_t word_count) const {
    // 一个张量区域 = 其所有 K 块密铺：num_k_blocks × word_count×16 字节。
    // 区域 stride 向上对齐 256B：保证后续区域基址 256B 对齐，burst(≤256B) 永不跨 4KB 页
    // （块内步长 word_count×16 均整除 4096：64/128/256/512B）。
    const uint64_t bytes = static_cast<uint64_t>(num_k_blocks()) * word_count * 16u;
    constexpr uint64_t kAlign = 256u;
    return (bytes + kAlign - 1u) / kAlign * kAlign;
}

uint64_t MatmulNtW4f16LsuOp::tensor_region_base(const std::string& name) const {
    if (slot_map_fpga()) {
        // FPGA 固定全局槽位（2026-09-30 图，与 FPGA 配置一致，每槽 256MB 间隔；
        // 逻辑地址：FPGA host 侧落点，RTL TB 的 1MB SRAM 不可达）。槽内 8 个 SMC 切片
        // 按统一步长 S=desc_.gr_intlv_addr 排布，硬件 +smc*intlv 自动选片。
        if (name == "a_tensor") return 0x0ULL;          // in0
        if (name == "b_tensor") return 0x10000000ULL;   // in1
        if (name == "scale_tensor") return 0x20000000ULL;  // in2
        if (name == "zp_tensor") return 0x30000000ULL;  // in3
    }
    uint64_t offset = 0;
    for (auto& [n, port] : desc_.inputs) {
        if (n == name) return offset + gr_tile_offset_;
        offset += region_stride_bytes(port.word_count);
    }
    return offset + gr_tile_offset_;
}

uint64_t MatmulNtW4f16LsuOp::image_region_base(const std::string& name) const {
    // memory_init 镜像里的紧凑偏移（与 rtl 槽位一致；fpga 模式镜像保持紧凑，
    // meta 同时给 byte_base（槽位）与 image_byte_base（镜像偏移）供 packer 重绑定）。
    uint64_t offset = 0;
    for (auto& [n, port] : desc_.inputs) {
        if (n == name) return offset;
        offset += region_stride_bytes(port.word_count);
    }
    return offset;
}

uint64_t MatmulNtW4f16LsuOp::tensor_block_base(const std::string& name, uint32_t kb) const {
    auto it = desc_.inputs.find(name);
    const uint64_t chunk =
        (it != desc_.inputs.end()) ? static_cast<uint64_t>(it->second.word_count) * 16u : 0u;
    return tensor_region_base(name) + static_cast<uint64_t>(kb) * chunk;
}

uint64_t MatmulNtW4f16LsuOp::store_gr_base() const {
    if (slot_map_fpga()) return 0x70000000ULL;   // FPGA 固定全局图：out0 槽位
    // store 落在所有输入区域之后（区域 stride 已 256B 对齐，末端天然对齐）。
    uint64_t offset = 0;
    for (auto& [name, port] : desc_.inputs) {
        (void)name;
        offset += region_stride_bytes(port.word_count);
    }
    return offset + gr_tile_offset_;
}

void MatmulNtW4f16LsuOp::append_load_port_iq(std::vector<op::IqInstructionFile>& all,
                                  uint8_t bank,
                                  uint16_t ur_word_offset,
                                  uint16_t word_count,
                                  uint64_t gr_base,
                                  uint8_t inst_id) const {
    auto p = build_load_params(bank, ur_word_offset, word_count, gr_base);
    p.para_inst_id = inst_id;
    if (!desc_.bulk_mode) {
        auto files = op::emit_load_iq_files(p);
        for (auto& f : files) all.push_back(std::move(f));
        return;
    }
    {
        auto lf = op::emit_load_iq_files(p);
        for (auto& f : lf) {
            if (f.para_iq_id == iq::IQ_ID_LDB) all.push_back(std::move(f));
        }
    }
    for (auto& seg : bulk_burst_segments(word_count)) {
        auto ps = p;
        ps.para_brst = static_cast<uint8_t>((seg.beats - 1) & 0x0F);
        ps.para_ldbaxi_loops = static_cast<uint16_t>(seg.num - 1);
        ps.para_ldbaxi_inc = 0x0100;
        ps.para_gr_base_addr = gr_base + seg.byte_off;
        auto sf = op::emit_load_iq_files(ps);
        for (auto& f : sf) {
            if (f.para_iq_id == iq::IQ_ID_LDBAXI) all.push_back(std::move(f));
        }
    }
}

std::vector<op::IqInstructionFile> MatmulNtW4f16LsuOp::generate_load_iq_files_for_k_block(
    uint32_t k_block) const {
    return generate_load_iq_files_for_k_batch(k_block, 1u);
}

std::vector<op::IqInstructionFile> MatmulNtW4f16LsuOp::generate_load_iq_files_for_k_batch(
    uint32_t kb_begin, uint32_t batch_n) const {
    std::vector<op::IqInstructionFile> all;
    if (batch_n == 0) return all;

    for (uint32_t i = 0; i < batch_n; ++i) {
        const uint32_t kb = kb_begin + i;
        const uint8_t inst_id = static_cast<uint8_t>(kb & 0xFu);

        // A/B：第 i 块写到 UR 连续槽位 word_offset + i*words；HBM 源 = 各自张量区域内第 kb 块
        for (const char* name : {"a_tensor", "b_tensor"}) {
            auto it = desc_.inputs.find(name);
            if (it == desc_.inputs.end()) continue;
            const auto& port = it->second;
            const uint16_t ur_off = static_cast<uint16_t>(
                port.word_offset + i * port.word_count);
            append_load_port_iq(all, port.bank, ur_off, port.word_count,
                                tensor_block_base(name, kb), inst_id);
        }
    }

    // zp/scale：GPTQ 才灌；fp16 模式跳过。
    if (!is_fp16()) {
        const uint32_t gsz = std::max<uint32_t>(1u, quant_group_size());
        const uint32_t chunk = k_chunk();
        const uint32_t g0 = (kb_begin * chunk) / gsz;
        for (uint32_t i = 0; i < batch_n; ++i) {
            const uint32_t kb = kb_begin + i;
            const uint32_t g = (kb * chunk) / gsz;
            const uint32_t slot = g - g0;
            const uint16_t ur_off = static_cast<uint16_t>(slot * kZpWordsPerSmc);
            const uint8_t inst_id = static_cast<uint8_t>(kb & 0xFu);
            for (const char* name : {"zp_tensor", "scale_tensor"}) {
                auto it = desc_.inputs.find(name);
                if (it == desc_.inputs.end()) continue;
                const auto& port = it->second;
                append_load_port_iq(all, port.bank, ur_off, port.word_count,
                                    tensor_block_base(name, kb), inst_id);
            }
        }
    }
    return all;
}

std::vector<op::IqInstructionFile> MatmulNtW4f16LsuOp::generate_compute_iq_files() const {
    return generate_compute_iq_files_for_k_block(0, 0, 0, 0, 0);
}

std::vector<op::IqInstructionFile> MatmulNtW4f16LsuOp::generate_compute_iq_files_for_k_block(
    uint8_t a_addr, uint8_t b_addr, uint32_t k_block,
    uint8_t zp_addr, uint8_t scale_addr, uint8_t out_addr) const {
    app::AppOperatorRequest req;
    std::mt19937 rng(cfg_.seed_base);
    std::uniform_real_distribution<float> data_dist(-1.0f, 1.0f);
    op::MatmulNtW4f16Op mm_op;
    mm_op.fill_random_request(req, rng, data_dist, "fp16");

    auto set_i32 = [&](const std::string& key, int32_t v) {
        app::AppTensorDesc d;
        d.para_data_i32.clear();
        d.para_data_i32.push_back(v);
        req.para_parameters[key] = d;
    };
    // M=256：每个 compute stage 是已验证的 M=8 微内核（D1' 组=独立 stage），
    // 组差异只体现在 a_addr/out_addr 偏移，compile_core 走逐比特同构路径。
    set_i32("M", static_cast<int32_t>(is_m256() ? 8u : m_rows()));
    set_i32("K", static_cast<int32_t>(k_chunk()));
    if (is_m1()) set_i32("K_tile", static_cast<int32_t>(op::kMatmulNtW4f16M1KTile));
    set_i32("a_addr", a_addr);
    set_i32("b_addr", b_addr);
    set_i32("input_addr", a_addr);
    set_i32("other_addr", b_addr);
    set_i32("out_addr", out_addr);
    set_i32("k_block", static_cast<int32_t>(k_block));
    set_i32("scale_addr", scale_addr);
    set_i32("zp_addr", zp_addr);
    set_i32("group_size", static_cast<int32_t>(quant_group_size()));
    set_i32("quantized", is_fp16() ? 0 : 1);

    std::unique_ptr<op::OpInstructionsBase> inst(mm_op.compile(req));
    return inst->get_iq_files();
}

// M=256 f16 epilogue stage（D4）：bank7 的 [2*word_base, 2*word_base+2*words) 行
// → bank6 的 [word_base, word_base+words) 个 packed f16 word。
std::vector<op::IqInstructionFile> MatmulNtW4f16LsuOp::generate_f16_epilogue_iq_files(
    uint32_t word_base, uint32_t words) const {
    app::AppOperatorRequest req;
    std::mt19937 rng(cfg_.seed_base);
    std::uniform_real_distribution<float> data_dist(-1.0f, 1.0f);
    op::MatmulNtW4f16Op mm_op;
    mm_op.fill_random_request(req, rng, data_dist, "fp16");

    auto set_i32 = [&](const std::string& key, int32_t v) {
        app::AppTensorDesc d;
        d.para_data_i32.clear();
        d.para_data_i32.push_back(v);
        req.para_parameters[key] = d;
    };
    // epilogue 与 M 维无关（纯 bank7→bank6 打包），M/K 只用于过 parse_request 的校验
    // ——固定取合法值 M=8/K=32；不能传 k_chunk()：M=1 时 k_chunk=256 过不了 validate_k。
    set_i32("M", 8);
    set_i32("K", 32);
    set_i32("ep_words", static_cast<int32_t>(words));
    set_i32("ep_src_addr", static_cast<int32_t>(word_base * 2u));
    set_i32("ep_dst_addr", static_cast<int32_t>(word_base));
    set_i32("ep_src_bank", 7);
    set_i32("ep_dst_bank", static_cast<int32_t>(desc_.output.bank));

    std::unique_ptr<op::OpInstructionsBase> inst(mm_op.compile(req));
    return inst->get_iq_files();
}

uint64_t MatmulNtW4f16LsuOp::zero_region_base() const {
    // FPGA 固定全局图：in4 槽位。注意不能沿用 store+0x10000：该图下即 out0 内部
    // 0x70010000，多 tile 时输出 out0+t*16KB 从 tile_4 起与之碰撞。
    if (slot_map_fpga()) return 0x40000000ULL;
    // D6：零区落在 store 区之后 64KB（256 word = 4KB，留足间隔避免与 store 落点互踩）。
    return store_gr_base() + 0x10000u;
}

std::vector<op::IqInstructionFile> MatmulNtW4f16LsuOp::generate_bank7_clear_iq_files() const {
    // 清零 bank7 累加区：M=256 → 256 word；M=8+f16 → 8 word（只清用到的行）；
    // M=1+f16 → 4 word（D8：epilogue 以 2-word 对读 bank7[0..3]，多读的 word1..3 必须
    // 确定为零；同时避开单拍 LDBAXI 挂死）。
    const uint16_t kBankWords =
        static_cast<uint16_t>(is_m256() ? 256u : (is_m1() ? 4u : m_rows()));
    std::vector<op::IqInstructionFile> all;
    if (desc_.bulk_mode) {
        append_bulk_load(all, 7u, 0u, kBankWords, zero_region_base());
        return all;
    }
    auto p = build_load_params(7u, 0u, kBankWords, zero_region_base());
    for (auto& f : op::emit_load_iq_files(p)) all.push_back(std::move(f));
    return all;
}

std::vector<op::IqInstructionFile> MatmulNtW4f16LsuOp::generate_store_iq_files() const {
    // RTL/默认（含 RTL fp32）走基类 per-channel store；FPGA 槽位图无论 f16/f32 都走
    // 0x20 拼接 emitter（fp32 时 word0=4×fp32、word1=零垫，burst 结构与 f16 M=1 同形）。
    if (!out_f16() && !slot_map_fpga()) return LsuOpBase::generate_store_iq_files();
    // D5：bank6 的 M/2 个 packed f16 word → words/2 个 256B 块（每块 = 4 行 × 32 列 f16）。
    // STB 一次搬完整 128 word（ur_rd_line_cnt=128）后由 STBAXI 匀速排空：
    //   第 k 个 burst 的 2 个 word（行对 2k/2k+1）落到 gr_base + k*256，
    //   SMC s 的 32B 落在块内偏移 s*32（gr_intlv_addr=0x20，见 default_compute_desc）。
    // 2 拍(32B) 是 LDBAXI/STBAXI 的最小安全 burst（单拍 brst=0 会挂死）。
    const uint16_t words = desc_.output.word_count;
    std::vector<op::IqInstructionFile> all;
    const uint64_t gr_base = store_gr_base();
    auto p = build_store_params(desc_.output.bank, desc_.output.word_offset, words, gr_base);
    for (auto& f : op::emit_store_iq_files(p)) {
        if (f.para_iq_id == iq::IQ_ID_STB) all.push_back(std::move(f));
    }
    const uint16_t bursts = static_cast<uint16_t>(words / 2u);
    for (uint16_t k = 0; k < bursts; ++k) {
        auto ps = p;
        ps.para_brst = 1;  // 2 拍 = 32B
        ps.para_stbaxi_loops = 0;
        ps.para_stbaxi_inc = 0x0100;  // 段间 256B
        ps.para_gr_base_addr = gr_base + static_cast<uint64_t>(k) * 256u;
        for (auto& f : op::emit_store_iq_files(ps)) {
            if (f.para_iq_id == iq::IQ_ID_STBAXI) all.push_back(std::move(f));
        }
    }
    return all;
}

uint16_t MatmulNtW4f16LsuOp::compute_body_cycles() const {
    app::AppOperatorRequest req;
    std::mt19937 rng(cfg_.seed_base);
    std::uniform_real_distribution<float> data_dist(-1.0f, 1.0f);
    op::MatmulNtW4f16Op mm_op;
    mm_op.fill_random_request(req, rng, data_dist, "fp16");

    app::AppTensorDesc k_param;
    k_param.para_data_i32.push_back(static_cast<int32_t>(k_chunk()));
    req.para_parameters["K"] = k_param;
    {
        app::AppTensorDesc m_param;
        m_param.para_data_i32.push_back(static_cast<int32_t>(m_rows()));
        req.para_parameters["M"] = m_param;
    }
    if (is_m1()) {
        app::AppTensorDesc kt;
        kt.para_data_i32.push_back(static_cast<int32_t>(op::kMatmulNtW4f16M1KTile));
        req.para_parameters["K_tile"] = kt;
    }
    {
        app::AppTensorDesc q;
        q.para_data_i32.push_back(is_fp16() ? 0 : 1);
        req.para_parameters["quantized"] = q;
    }

    std::unique_ptr<op::OpInstructionsBase> inst(mm_op.compile(req));
    return static_cast<uint16_t>(std::max<uint32_t>(1u, inst->get_total_steps()));
}

uint16_t MatmulNtW4f16LsuOp::load_block_cycles() const {
    if (const char* e = std::getenv("MM_LOAD_CYCLES")) {
        const int v = std::atoi(e);
        if (v > 0 && v <= 65535) return static_cast<uint16_t>(v);
    }
    return 64u;
}

uint16_t MatmulNtW4f16LsuOp::compute_after_load_gap() const {
    if (const char* e = std::getenv("MM_COMPUTE_GAP")) {
        const int v = std::atoi(e);
        if (v >= 0 && v <= 65535) return static_cast<uint16_t>(v);
    }
    return 10u;
}

void MatmulNtW4f16LsuOp::extract_zp_scale_cols(std::vector<uint8_t>& zp_col,
                                      std::vector<float>& scale_col,
                                      uint32_t k_block) const {
    const size_t kDim = static_cast<size_t>(n_total());   // 全局 N 宽（列窗 col_base 由调用方给 packer）
    zp_col.assign(kDim, 0u);
    scale_col.assign(kDim, 1.0f);

    ensure_external_loaded();
    if (ext_.valid) {
        const uint32_t scale_group = (k_block * k_chunk()) / ext_.group_size;
        if (scale_group >= ext_.num_groups) return;
        const size_t base = static_cast<size_t>(scale_group) * kDim;
        for (size_t c = 0; c < kDim; ++c) {
            zp_col[c] = static_cast<uint8_t>(ext_.zp[base + c] & 0xFu);
            scale_col[c] = ext_.scale[base + c];
        }
        return;
    }

    app::AppOperatorRequest req;
    std::mt19937 rng(cfg_.seed_base);
    std::uniform_real_distribution<float> data_dist(-1.0f, 1.0f);
    op::MatmulNtW4f16Op mm_op;
    mm_op.fill_random_request(req, rng, data_dist, "fp16");
    const auto zp_it = req.para_inputs.find("zp_tensor");
    const auto sc_it = req.para_inputs.find("scale_tensor");
    if (zp_it != req.para_inputs.end()) {
        for (size_t c = 0; c < kDim && c < zp_it->second.para_data_i32.size(); ++c) {
            zp_col[c] = static_cast<uint8_t>(zp_it->second.para_data_i32[c] & 0xFu);
        }
    }
    if (sc_it != req.para_inputs.end()) {
        for (size_t c = 0; c < kDim && c < sc_it->second.para_data_f32.size(); ++c) {
            scale_col[c] = sc_it->second.para_data_f32[c];
        }
    }
    (void)k_block;
}

void MatmulNtW4f16LsuOp::build_test_tensors(uint8_t smc_id, uint32_t seed,
                                   std::vector<float>& a,
                                   std::vector<uint8_t>& bq_u4,
                                   std::vector<uint8_t>& zp_bank0,
                                   std::vector<uint8_t>& scale_bank0) const {
    std::vector<float> b_fp32_unused;
    build_test_tensors(smc_id, seed, a, bq_u4, b_fp32_unused, zp_bank0, scale_bank0);
}

void MatmulNtW4f16LsuOp::build_test_tensors(uint8_t smc_id, uint32_t seed,
                                   std::vector<float>& a,
                                   std::vector<uint8_t>& bq_u4,
                                   std::vector<float>& b_fp32,
                                   std::vector<uint8_t>& zp_bank0,
                                   std::vector<uint8_t>& scale_bank0) const {
    ensure_external_loaded();
    b_fp32.clear();
    if (ext_.valid) {
        a = ext_.a;
        bq_u4 = ext_.bq;
        b_fp32 = ext_.b_fp32;
    } else {
        app::AppOperatorRequest req;
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> data_dist(-1.0f, 1.0f);
        op::MatmulNtW4f16Op mm_op;
        mm_op.fill_random_request(req, rng, data_dist, "fp16");

        const auto a_it = req.para_inputs.find("a_tensor");
        const auto b_it = req.para_inputs.find("b_tensor");
        if (a_it == req.para_inputs.end() || b_it == req.para_inputs.end()) return;

        a = a_it->second.para_data_f32;
        constexpr size_t kNCols = 64u;
        const size_t kTot = static_cast<size_t>(k_total());
        if (is_fp16()) {
            b_fp32 = b_it->second.para_data_f32;
            if (b_fp32.size() < kTot * kNCols) {
                b_fp32.resize(kTot * kNCols, 0.0f);
            }
            bq_u4.clear();
        } else {
            bq_u4.assign(kTot * kNCols, 0u);
            for (size_t i = 0; i < bq_u4.size() && i < b_it->second.para_data_i32.size(); ++i) {
                const int32_t v = b_it->second.para_data_i32[i];
                bq_u4[i] = static_cast<uint8_t>(v & 0xFu);
            }
        }
    }

    zp_bank0.assign(static_cast<size_t>(kZpWordsPerSmc) * 16u, 0u);
    scale_bank0.assign(static_cast<size_t>(kScaleWordsPerSmc) * 16u, 0u);
    if (is_fp16()) return;

    std::vector<uint8_t> zp_col;
    std::vector<float> scale_col;
    extract_zp_scale_cols(zp_col, scale_col, 0u);

    std::vector<std::vector<uint8_t>> zp_smc;
    std::vector<std::vector<uint8_t>> scale_smc;
    op::MatmulNtW4f16Op::pack_zp_dense_interleave(zp_col, desc_.num_smc, kZpWordsPerSmc, zp_smc,
                                                  col_base());
    op::MatmulNtW4f16Op::pack_scale_dense_interleave(scale_col, desc_.num_smc, kScaleWordsPerSmc,
                                                     scale_smc, col_base());

    if (smc_id < zp_smc.size()) {
        const size_t n = std::min(zp_bank0.size(), zp_smc[smc_id].size());
        std::copy(zp_smc[smc_id].begin(), zp_smc[smc_id].begin() + static_cast<ptrdiff_t>(n),
                  zp_bank0.begin());
    }
    if (smc_id < scale_smc.size()) {
        const size_t n = std::min(scale_bank0.size(), scale_smc[smc_id].size());
        std::copy(scale_smc[smc_id].begin(), scale_smc[smc_id].begin() + static_cast<ptrdiff_t>(n),
                  scale_bank0.begin());
    }
}

void MatmulNtW4f16LsuOp::generate_hbm_data(uint8_t smc_id, uint32_t seed,
                                  std::vector<uint8_t>& hbm_bytes) const {
    std::vector<float> a;
    std::vector<uint8_t> bq_u4;
    std::vector<float> b_fp32;
    std::vector<uint8_t> zp_dummy;
    std::vector<uint8_t> scale_dummy;
    build_test_tensors(smc_id, seed, a, bq_u4, b_fp32, zp_dummy, scale_dummy);

    // HBM init 覆盖镜像（紧凑布局，与 rtl 槽位一致）：[0, 所有输入区域末端)。
    // fpga 槽位模式下镜像同样保持紧凑（meta 同时给 byte_base 与 image_byte_base），
    // 32MB 槽位由 packer 重绑定时落位。
    uint64_t image_end = 0;
    for (auto& [nm, port] : desc_.inputs) {
        (void)nm;
        image_end += region_stride_bytes(port.word_count);
    }
    const uint64_t total_bytes = image_end + gr_tile_offset_;
    hbm_bytes.assign(total_bytes, 0u);
    if (out_f16()) {
        // D6：bank7 清零读的零区（store 区之后 64KB）也要在 memory_init 里显式为 0，
        // 否则 TB 的 SRAM 未初始化（X）会被 LDB 搬进 bank7。
        const uint64_t zero_end = image_end + 0x10000u + 256u * 16u;
        if (hbm_bytes.size() < zero_end) hbm_bytes.resize(static_cast<size_t>(zero_end), 0u);
    }

    // DDR/HBM 布局（多 stage 与扁平统一，tensor-major）：每个张量的所有 K 块密铺成一个
    // 连续区域（A 区 | B 区 | scale 区 | zp 区，区域基址 256B 对齐），区域内按 K 块顺序排布。
    // A、B 各自「列优先密铺」+ 连续列置换 + 2-word 间插。
    // 间插2 分发下 SMC s 拿到原矩阵的连续 4 列 {4s, 4s+1, 4s+2, 4s+3}（smc0 = {0,1,2,3}）。
    std::vector<std::vector<uint8_t>> smc_a;
    std::vector<std::vector<uint8_t>> smc_b;
    // 镜像内块基址（紧凑偏移；fpga 槽位下与 IQ 地址分离，见 image_region_base 注释）。
    auto image_block_base = [&](const std::string& name, uint32_t kb) {
        auto it = desc_.inputs.find(name);
        const uint64_t chunk =
            (it != desc_.inputs.end()) ? static_cast<uint64_t>(it->second.word_count) * 16u : 0u;
        return image_region_base(name) + static_cast<uint64_t>(kb) * chunk;
    };
    const uint32_t n_stride = n_total();
    const uint32_t cb = col_base();
    for (uint32_t kb = 0; kb < num_k_blocks(); ++kb) {
        if (is_fp16()) {
            op::MatmulNtW4f16Op::pack_a_dense_interleave_kblock(a, desc_.num_smc, a_words_per_smc(), kb, smc_a);
            std::vector<uint16_t> b_bits(b_fp32.size(), 0u);
            for (size_t i = 0; i < b_fp32.size(); ++i) {
                b_bits[i] = app::float32_to_float16(b_fp32[i]);
            }
            op::MatmulNtW4f16Op::pack_b_fp16_dense_interleave_kblock(b_bits, desc_.num_smc, b_words_per_smc(),
                                                           kb, smc_b, n_stride, cb);
        } else if (is_m1()) {
            op::MatmulNtW4f16Op::pack_a_m1_dense_interleave_kblock(a, desc_.num_smc, a_words_per_smc(), kb,
                                                          smc_a, /*replicate=*/false);
            op::MatmulNtW4f16Op::pack_bq_m1_dense_interleave_kblock(bq_u4, desc_.num_smc, b_words_per_smc(),
                                                           kb, smc_b, n_stride, cb);
        } else if (is_m256()) {
            // M=256：A = 32 组 Aᵀ 块拼接后 2-word 间插（128 word/SMC）；B 与 M=8 相同。
            op::MatmulNtW4f16Op::pack_a_m256_dense_interleave_kblock(a, desc_.num_smc, a_words_per_smc(),
                                                            kb, smc_a);
            op::MatmulNtW4f16Op::pack_bq_dense_interleave_kblock(bq_u4, desc_.num_smc, b_words_per_smc(), kb,
                                                       smc_b, n_stride, cb);
        } else {
            op::MatmulNtW4f16Op::pack_a_dense_interleave_kblock(a, desc_.num_smc, a_words_per_smc(), kb, smc_a);
            op::MatmulNtW4f16Op::pack_bq_dense_interleave_kblock(bq_u4, desc_.num_smc, b_words_per_smc(), kb,
                                                       smc_b, n_stride, cb);
        }
        if (smc_a.size() <= smc_id || smc_b.size() <= smc_id) continue;

        const uint64_t a_base = image_block_base("a_tensor", kb);
        const uint64_t b_base = image_block_base("b_tensor", kb);
        const size_t a_n =
            std::min(smc_a[smc_id].size(), static_cast<size_t>(a_words_per_smc()) * 16u);
        const size_t b_n =
            std::min(smc_b[smc_id].size(), static_cast<size_t>(b_words_per_smc()) * 16u);
        std::copy(smc_a[smc_id].begin(), smc_a[smc_id].begin() + static_cast<ptrdiff_t>(a_n),
                  hbm_bytes.begin() + static_cast<ptrdiff_t>(a_base));
        std::copy(smc_b[smc_id].begin(), smc_b[smc_id].begin() + static_cast<ptrdiff_t>(b_n),
                  hbm_bytes.begin() + static_cast<ptrdiff_t>(b_base));

        if (smc_id == 0 && std::getenv("MM_DUMP_GOLDEN_BLOCKS")) {
            auto dump_rev16 = [](const char* tag, uint32_t kb2, const std::vector<uint8_t>& v) {
                std::fprintf(stderr, "[MS_DBG][hbm] smc0 kb%u %s word0=", kb2, tag);
                for (int i = 15; i >= 0 && i < static_cast<int>(v.size()); --i)
                    std::fprintf(stderr, "%02x", v[static_cast<size_t>(i)]);
                std::fprintf(stderr, "\n");
            };
            dump_rev16("bank0(A)", kb, smc_a[smc_id]);
            dump_rev16(is_fp16() ? "bank1(Bfp16)" : "bank1(Bq)", kb, smc_b[smc_id]);
        }

        if (is_fp16()) continue;

        // zp/scale：随 K 块切换量化组（GPTQ group_size=64）。
        // M=8：每 kb 写 1 组。
        // M=1：K_tile=256 含 4 组，按组序密铺到 zp/scale 区（槽0..3 → word 0,4,8,12）。
        if (is_m1()) {
            const uint32_t gsz = std::max<uint32_t>(1u, quant_group_size());
            const uint32_t groups_per_tile = std::max<uint32_t>(1u, k_chunk() / gsz);
            const uint64_t zp_base = image_block_base("zp_tensor", kb);
            const uint64_t sc_base = image_block_base("scale_tensor", kb);
            for (uint32_t gi = 0; gi < groups_per_tile; ++gi) {
                const uint32_t abs_g = (kb * k_chunk()) / gsz + gi;
                std::vector<uint8_t> zp_col;
                std::vector<float> scale_col;
                // 复用 extract：临时用「假 k_block」使 scale_group=abs_g
                // extract 公式： (k_block * k_chunk) / gsz == abs_g → k_block = abs_g * gsz / k_chunk
                // 对 M=1：abs_g * 64 / 256 = abs_g / 4，仅当 abs_g%4==0 时精确。
                // 改为直接从 ext_ / 单组填充：
                zp_col.assign(n_total(), 0u);
                scale_col.assign(n_total(), 1.0f);
                ensure_external_loaded();
                if (ext_.valid && abs_g < ext_.num_groups) {
                    const size_t base = static_cast<size_t>(abs_g) * n_total();
                    for (size_t c = 0; c < n_total(); ++c) {
                        zp_col[c] = static_cast<uint8_t>(ext_.zp[base + c] & 0xFu);
                        scale_col[c] = ext_.scale[base + c];
                    }
                } else {
                    extract_zp_scale_cols(zp_col, scale_col, kb);  // random：各组相同
                }
                std::vector<std::vector<uint8_t>> zp_smc, scale_smc;
                op::MatmulNtW4f16Op::pack_zp_dense_interleave(zp_col, desc_.num_smc, kZpWordsPerSmc, zp_smc, cb);
                op::MatmulNtW4f16Op::pack_scale_dense_interleave(scale_col, desc_.num_smc, kScaleWordsPerSmc,
                                                        scale_smc, cb);
                const uint64_t slot_off =
                    static_cast<uint64_t>(gi) * static_cast<uint64_t>(kZpWordsPerSmc) * 16u;
                if (smc_id < zp_smc.size() && desc_.inputs.count("zp_tensor")) {
                    const size_t zp_n = std::min(zp_smc[smc_id].size(),
                                                 static_cast<size_t>(kZpWordsPerSmc) * 16u);
                    std::copy(zp_smc[smc_id].begin(),
                              zp_smc[smc_id].begin() + static_cast<ptrdiff_t>(zp_n),
                              hbm_bytes.begin() + static_cast<ptrdiff_t>(zp_base + slot_off));
                }
                if (smc_id < scale_smc.size() && desc_.inputs.count("scale_tensor")) {
                    const size_t sc_n = std::min(scale_smc[smc_id].size(),
                                                 static_cast<size_t>(kScaleWordsPerSmc) * 16u);
                    std::copy(scale_smc[smc_id].begin(),
                              scale_smc[smc_id].begin() + static_cast<ptrdiff_t>(sc_n),
                              hbm_bytes.begin() + static_cast<ptrdiff_t>(sc_base + slot_off));
                }
            }
        } else {
        std::vector<uint8_t> zp_col;
        std::vector<float> scale_col;
        extract_zp_scale_cols(zp_col, scale_col, kb);
        std::vector<std::vector<uint8_t>> zp_smc;
        std::vector<std::vector<uint8_t>> scale_smc;
        op::MatmulNtW4f16Op::pack_zp_dense_interleave(zp_col, desc_.num_smc, kZpWordsPerSmc, zp_smc, cb);
        op::MatmulNtW4f16Op::pack_scale_dense_interleave(scale_col, desc_.num_smc, kScaleWordsPerSmc,
                                                scale_smc, cb);
        if (smc_id < zp_smc.size() && desc_.inputs.count("zp_tensor")) {
            const uint64_t zp_base = image_block_base("zp_tensor", kb);
            const size_t zp_n = std::min(zp_smc[smc_id].size(),
                                         static_cast<size_t>(kZpWordsPerSmc) * 16u);
            std::copy(zp_smc[smc_id].begin(), zp_smc[smc_id].begin() + static_cast<ptrdiff_t>(zp_n),
                      hbm_bytes.begin() + static_cast<ptrdiff_t>(zp_base));
        }
        if (smc_id < scale_smc.size() && desc_.inputs.count("scale_tensor")) {
            const uint64_t sc_base = image_block_base("scale_tensor", kb);
            const size_t sc_n = std::min(scale_smc[smc_id].size(),
                                         static_cast<size_t>(kScaleWordsPerSmc) * 16u);
            std::copy(scale_smc[smc_id].begin(),
                      scale_smc[smc_id].begin() + static_cast<ptrdiff_t>(sc_n),
                      hbm_bytes.begin() + static_cast<ptrdiff_t>(sc_base));
        }
        }  // !is_m1
    }
}

void MatmulNtW4f16LsuOp::generate_golden_for_smc(
    uint8_t smc_id,
    const std::map<std::string, std::vector<uint8_t>>& /*input_datas*/,
    std::vector<uint8_t>& golden_out) const {
    std::vector<float> a;
    std::vector<uint8_t> bq_u4;
    std::vector<float> b_fp32;
    std::vector<uint8_t> zp_bank0;
    std::vector<uint8_t> scale_bank0;
    build_test_tensors(smc_id, cfg_.seed_base, a, bq_u4, b_fp32, zp_bank0, scale_bank0);

    // K 全归约：逐 K 块算 partial（GPTQ 已×scale；fp16 无 scale），再 fp32 累加到 bank7。
    const uint32_t nkb = num_k_blocks();

    // f16 packed 打包（D4/D5）：acc_f[row*4+n] = 列 4s+(3−n)（blk/bank7 值字节序）→
    // bank6 字 r（行对 2r/2r+1）字节组 n = {LE16 f16(行 2r), LE16 f16(行 2r+1)}。
    // fp16 转换用 app::float32_to_float16（RNE，与 RTL fp32_to_fp16.v 逐位一致）。
    auto pack_f16 = [&](const std::vector<float>& acc_f, uint32_t ow) {
        golden_out.assign(static_cast<size_t>(ow) * 16u, 0u);
        for (uint32_t r = 0; r < ow; ++r) {
            for (uint32_t n = 0; n < 4u; ++n) {
                const uint16_t lo = app::float32_to_float16(acc_f[(2u * r) * 4u + n]);
                const uint16_t hi = app::float32_to_float16(acc_f[(2u * r + 1u) * 4u + n]);
                const size_t off = static_cast<size_t>(r) * 16u + n * 4u;
                golden_out[off + 0] = static_cast<uint8_t>(lo & 0xFFu);
                golden_out[off + 1] = static_cast<uint8_t>((lo >> 8) & 0xFFu);
                golden_out[off + 2] = static_cast<uint8_t>(hi & 0xFFu);
                golden_out[off + 3] = static_cast<uint8_t>((hi >> 8) & 0xFFu);
            }
        }
    };

    // M=256（D1'）：32 个 row-group × 全 K 归约（fp32，语义同 bank7），再打包 f16。
    if (is_m256()) {
        std::vector<float> acc7(op::kMatmulNtW4f16M256 * 4u, 0.0f);  // [row*4+n] = 列 4s+(3−n)
        for (uint32_t kb = 0; kb < nkb; ++kb) {
            std::vector<uint8_t> zp_col;
            std::vector<float> scale_col;
            extract_zp_scale_cols(zp_col, scale_col, kb);
            for (uint32_t g = 0; g < op::kMatmulNtW4f16M256Groups; ++g) {
                std::vector<uint8_t> blk;
                if (!op::MatmulNtW4f16Op::compute_bank2_golden_kchunk(
                        smc_id, a, bq_u4, zp_col, scale_col, kb, blk,
                        op::kMatmulNtW4f16M256,
                        g * op::kMatmulNtW4f16M256RowsPerGroup,
                        n_total(), col_base())) {
                    golden_out.assign(static_cast<size_t>(out_words()) * 16u, 0u);
                    return;
                }
                // blk 字节偏移 4n = 列 4s+(3−n) 的 fp32 LE（与 bank7 值字节约定一致）
                for (uint32_t row = 0; row < op::kMatmulNtW4f16M256RowsPerGroup; ++row) {
                    for (uint32_t n = 0; n < 4u; ++n) {
                        const size_t off = static_cast<size_t>(row) * 16u + n * 4u;
                        uint32_t bits = static_cast<uint32_t>(blk[off]) |
                                        (static_cast<uint32_t>(blk[off + 1]) << 8) |
                                        (static_cast<uint32_t>(blk[off + 2]) << 16) |
                                        (static_cast<uint32_t>(blk[off + 3]) << 24);
                        float v = 0.0f;
                        std::memcpy(&v, &bits, sizeof(v));
                        acc7[static_cast<size_t>(g * op::kMatmulNtW4f16M256RowsPerGroup + row) *
                                 4u +
                             n] += v;
                    }
                }
            }
        }
        pack_f16(acc7, out_words());
        return;
    }

    std::vector<float> acc;
    for (uint32_t kb = 0; kb < nkb; ++kb) {
        std::vector<uint8_t> blk;
        bool ok = false;
        if (is_fp16()) {
            ok = op::MatmulNtW4f16Op::compute_bank2_golden_fp16_kchunk(smc_id, a, b_fp32, kb, blk,
                                                                       n_total(), col_base());
        } else {
            std::vector<uint8_t> zp_col;
            std::vector<float> scale_col;
            extract_zp_scale_cols(zp_col, scale_col, kb);
            if (is_m1()) {
                ensure_external_loaded();
                if (ext_.valid) {
                    ok = op::MatmulNtW4f16Op::compute_bank2_golden_m1_ktile(
                        smc_id, a, bq_u4, ext_.zp, ext_.scale, kb, ext_.group_size, blk,
                        n_total(), col_base());
                } else {
                    ok = op::MatmulNtW4f16Op::compute_bank2_golden_m1_ktile(smc_id, a, bq_u4, zp_col, scale_col,
                                                                  kb, blk);
                }
            } else {
                ok = op::MatmulNtW4f16Op::compute_bank2_golden_kchunk(smc_id, a, bq_u4, zp_col, scale_col, kb,
                                                            blk, 8u, 0u, n_total(), col_base());
            }
        }
        if (!ok) {
            golden_out.assign(static_cast<size_t>(out_words()) * 16u, 0u);
            return;
        }
        const size_t nfloats = blk.size() / 4u;
        if (acc.empty()) acc.assign(nfloats, 0.0f);
        for (size_t i = 0; i < nfloats && i < acc.size(); ++i) {
            uint32_t bits = static_cast<uint32_t>(blk[i * 4 + 0]) |
                            (static_cast<uint32_t>(blk[i * 4 + 1]) << 8) |
                            (static_cast<uint32_t>(blk[i * 4 + 2]) << 16) |
                            (static_cast<uint32_t>(blk[i * 4 + 3]) << 24);
            float v = 0.0f;
            std::memcpy(&v, &bits, sizeof(v));
            acc[i] += v;
        }
        if (smc_id == 0 && std::getenv("MM_DUMP_GOLDEN_BLOCKS")) {
            std::fprintf(stderr, "[MS_DBG][golden] smc0 after kb%u  bank7[0] lanes=", kb);
            for (size_t i = 0; i < 4u && i < acc.size(); ++i) {
                uint32_t b = 0; std::memcpy(&b, &acc[i], sizeof(b));
                std::fprintf(stderr, "%08x ", b);
            }
            std::fprintf(stderr, "\n");
        }
    }
    if (acc.empty()) {
        golden_out.assign(static_cast<size_t>(out_words()) * 16u, 0u);
        return;
    }
    // M=8 + MM_OUT_DTYPE=f16（阶梯 1 快验）：acc 已是 blk 序（[row*4+n] = 列 4s+(3−n)），
    // 与 M=256 共用 pack_f16 打包成 bank6 的 4 个 packed f16 word。
    if (out_f16()) {
        // M=1（D8）：acc 只有 4 个 fp32 lane（1 word）；pack_f16(out_words=2) 按行对读
        // (2r,2r+1)*4+n，需 16 个 lane——不存在的行对补 0，与「bank7 word1..3 恒零 →
        // f16(0)=0x0000」的 RTL 行为逐位一致。
        if (is_m1() && acc.size() < 16u) acc.resize(16u, 0.0f);
        pack_f16(acc, out_words());
        return;
    }
    // fp32 输出：M=1 逻辑输出 1 word，但 store 垫到 out_words=2（bank7 word1 恒 0，
    // 见 out_words() 注释）→ golden 同步按 out_words*16B 零垫，与 store 映像等长。
    // M=8：acc=32×4B=128B=out_words(8)×16B，等长无影响。
    golden_out.assign(static_cast<size_t>(out_words()) * 16u, 0u);
    for (size_t i = 0; i < acc.size(); ++i) {
        uint32_t bits = 0;
        std::memcpy(&bits, &acc[i], sizeof(bits));
        golden_out[i * 4 + 0] = static_cast<uint8_t>(bits & 0xFFu);
        golden_out[i * 4 + 1] = static_cast<uint8_t>((bits >> 8) & 0xFFu);
        golden_out[i * 4 + 2] = static_cast<uint8_t>((bits >> 16) & 0xFFu);
        golden_out[i * 4 + 3] = static_cast<uint8_t>((bits >> 24) & 0xFFu);
    }
}

bool MatmulNtW4f16LsuOp::assemble_full_case() const {
    // N-tile 列窗校验（Phase 4 / D3）：本 case 打包全局列 [col_base, col_base+32)，
    // 必须落在 [0, n_total) 内；列窗 >0 时 random 张量（N=64）无法覆盖，必须给 MM_TENSOR_DIR。
    if (col_base() + 32u > n_total()) {
        throw std::runtime_error("[MatmulNtW4f16LsuOp] MM_N_TILE_START=" +
                                 std::to_string(n_tile_idx()) + " out of range: col_base+32 > MM_N=" +
                                 std::to_string(n_total()));
    }
    if (n_tile_idx() > 0u) {
        ensure_external_loaded();
        if (!ext_.valid) {
            std::cerr << "[MatmulNtW4f16LsuOp] WARN: MM_N_TILE_START>0 without MM_TENSOR_DIR："
                         " random 张量仅 N=64，tile>1 的列窗数据不可靠（golden 会全零/失真）"
                      << std::endl;
        }
    }

    // M=1 f16（D8）只有多 stage 路径：bank7→bank6 的退化 epilogue stage 只存在于
    // assemble_multistage_case；扁平路径没有 epilogue，f16 store 会读未写过的 bank6。
    const char* ms_env = std::getenv("MM_MULTISTAGE");
    if (is_m1() && out_f16() && !(ms_env != nullptr && ms_env[0] == '1')) {
        throw std::runtime_error(
            "[MatmulNtW4f16LsuOp] M=1 + MM_OUT_DTYPE=f16 仅支持多 stage 流程，请设 MM_MULTISTAGE=1");
    }

    if (const char* e = std::getenv("MM_MULTISTAGE")) {
        if (e[0] == '1') return assemble_multistage_case();
    }

    const std::string case_root = cfg_.case_root;

    const uint16_t load_cycles = load_block_cycles();
    const uint16_t compute_cycles = compute_body_cycles();
    const uint16_t compute_gap = compute_after_load_gap();
    const uint32_t period = static_cast<uint32_t>(load_cycles) + static_cast<uint32_t>(compute_cycles);

    std::vector<op::IqInstructionFile> load_iqs;
    std::vector<op::IqInstructionFile> compute_iqs;
    const auto compute_body = generate_compute_iq_files();
    std::map<std::string, uint32_t> queue_pos;
    uint32_t compute_end_cycle = 0;  // 最后一个 k 块 compute 结束拍（store 需等其之后）

    for (uint32_t kb = 0; kb < num_k_blocks(); ++kb) {
        const uint16_t load_target = static_cast<uint16_t>(kb * period);
        // kb=0：ur_data 已预填 bank0/1（与 acb 一致），跳过 LDB，compute 从 cycle 0 启动。
        // kb>0：LDB 完成后再留 compute_gap 拍启动 compute（对齐 MM_K_DYNAMIC_PLAN §4.3）。
        const uint16_t compute_target =
            (kb == 0u) ? 0u
                         : static_cast<uint16_t>(load_target + load_cycles + compute_gap);
        compute_end_cycle = std::max<uint32_t>(
            compute_end_cycle, static_cast<uint32_t>(compute_target) + compute_cycles);

        if (kb > 0u) {
            auto lk = generate_load_iq_files_for_k_block(kb);
            std::set<std::string> load_prepended;
            for (auto& f : lk) {
                const std::string key = iq_queue_key(f);
                if (load_prepended.insert(key).second) {
                    const uint32_t pos = queue_pos[key];
                    if (load_target > pos) {
                        iq_push_idle_file(load_iqs, f, static_cast<uint16_t>(load_target - pos));
                    }
                    queue_pos[key] = static_cast<uint32_t>(load_target) + load_cycles;
                }
                load_iqs.push_back(std::move(f));
            }
        }

        std::set<std::string> compute_prepended;
        for (auto f : compute_body) {
            if (kb + 1u < num_k_blocks()) {
                iq_strip_jump_end(f);
            }
            const std::string key = iq_queue_key(f);
            if (compute_prepended.insert(key).second) {
                const uint32_t pos = queue_pos[key];
                if (compute_target > pos) {
                    iq_push_idle_file(compute_iqs, f, static_cast<uint16_t>(compute_target - pos));
                }
                queue_pos[key] = static_cast<uint32_t>(compute_target) + compute_cycles;
            }
            compute_iqs.push_back(std::move(f));
        }
    }

    // store：把 bank7 结果写回 DDR（STB/STBAXI）。注意 STB 的 IQ RAM 每条指令写到固定
    // AXI_ALIGN_ADDR，不能像算术队列那样前置 idle 指令（会与真正 STB 写到同一 RAM 地址而互相
    // 覆盖，破坏加载）。store 的发起延迟改用 STB/STBAXI 的 cond 字段等 compute 完成信号。
    (void)compute_end_cycle;
    auto store_iqs = generate_store_iq_files();

    // 多 SMC verification_v2：指令写在 case 根目录，供 generate_case_inst_list 扫描。
    write_iq_hex_files(case_root, load_iqs, compute_iqs, store_iqs);

    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data");
        mkdir_p(smc_dir + "/output_bank");

        std::vector<uint8_t> zp_bank0;
        std::vector<uint8_t> scale_bank0;
        std::vector<float> a_tensor;
        std::vector<uint8_t> bq_u4;
        std::vector<float> b_fp32;
        build_test_tensors(smc, cfg_.seed_base, a_tensor, bq_u4, b_fp32, zp_bank0, scale_bank0);

        std::vector<std::vector<uint8_t>> smc_a;
        std::vector<std::vector<uint8_t>> smc_b;
        if (is_fp16()) {
            op::MatmulNtW4f16Op::pack_a_dense_interleave_kblock(a_tensor, desc_.num_smc, a_words_per_smc(), 0,
                                                       smc_a);
            std::vector<uint16_t> b_bits(b_fp32.size(), 0u);
            for (size_t i = 0; i < b_fp32.size(); ++i) {
                b_bits[i] = app::float32_to_float16(b_fp32[i]);
            }
            op::MatmulNtW4f16Op::pack_b_fp16_dense_interleave_kblock(b_bits, desc_.num_smc, b_words_per_smc(),
                                                           0, smc_b, n_total(), col_base());
        } else if (is_m1()) {
            op::MatmulNtW4f16Op::pack_a_m1_dense_interleave_kblock(a_tensor, desc_.num_smc, a_words_per_smc(),
                                                          0, smc_a, /*replicate=*/false);
            op::MatmulNtW4f16Op::pack_bq_m1_dense_interleave_kblock(bq_u4, desc_.num_smc, b_words_per_smc(), 0,
                                                           smc_b, n_total(), col_base());
        } else {
            op::MatmulNtW4f16Op::pack_a_per_smc_kblock(a_tensor, desc_.num_smc, a_words_per_smc(), 0, smc_a);
            op::MatmulNtW4f16Op::pack_bq_u4_per_smc_kblock(bq_u4, desc_.num_smc, 0, smc_b);
        }

        for (int b = 0; b < 8; ++b) {
            const std::string bank_path = smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex";
            if (b == 0 && smc_a.size() > smc) {
                write_bank_with_prefix(bank_path, smc_a[smc], 256);
            } else if (b == 1 && smc_b.size() > smc) {
                write_bank_with_prefix(bank_path, smc_b[smc], 256);
            } else if (b == 4 && !is_fp16()) {
                write_bank_with_prefix(bank_path, zp_bank0, 256);
            } else if (b == 5 && !is_fp16()) {
                write_bank_with_prefix(bank_path, scale_bank0, 256);
            } else {
                write_zero_bank(bank_path, 256);
            }
        }

        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);
        const std::string hbm_path =
            cfg_.hbm_init_dir + "/memory_init" + std::to_string(smc + 1) + ".hex";
        write_hex_file(hbm_path, hbm_data, 16);

        std::vector<uint8_t> golden;
        generate_golden_for_smc(smc, {}, golden);
        if (golden.size() / 16 < 256u) golden.resize(256u * 16u, 0u);
        write_hex_file(smc_dir + "/output_bank/bank" + std::to_string(desc_.output.bank) +
                           "_final.hex",
                       golden, 16);
    }

    {
        std::ofstream mf(case_root + "/meta_v3.json");
        if (mf) {
            mf << "{\n";
            mf << "  \"op\": \"" << get_name() << "\",\n";
            mf << "  \"num_smc\": " << static_cast<int>(desc_.num_smc) << ",\n";
            mf << "  \"m_rows\": " << m_rows() << ",\n";
            mf << "  \"k_total\": " << k_total() << ",\n";
            mf << "  \"k_chunk\": " << k_chunk() << ",\n";
            mf << "  \"num_k_blocks\": " << num_k_blocks() << ",\n";
            mf << "  \"load_block_cycles\": " << load_cycles << ",\n";
            mf << "  \"compute_after_load_gap\": " << compute_gap << ",\n";
            mf << "  \"compute_body_cycles\": " << compute_cycles << ",\n";
            mf << "  \"n_total\": " << n_total() << ",\n";
            mf << "  \"n_tile_idx\": " << n_tile_idx() << ",\n";
            mf << "  \"col_base\": " << col_base() << ",\n";
            mf << "  \"slot_map\": \"" << (slot_map_fpga() ? "fpga" : "rtl") << "\",\n";
            if (slot_map_fpga()) {
                // FPGA 固定全局图（2026-09-30）：packer 按此把镜像切片打包成槽内单拷贝。
                mf << "  \"hbm_layout\": \"global_slots\",\n";
                mf << "  \"slice_stride\": " << desc_.gr_intlv_addr << ",\n";
                mf << "  \"zero_byte_base\": " << zero_region_base() << ",\n";
            }
            mf << "  \"layout\": \"tensor_major\",\n";
            mf << "  \"store_byte_base\": " << store_gr_base() << ",\n";
            mf << "  \"out_bank\": " << static_cast<int>(desc_.output.bank) << ",\n";
            mf << "  \"out_words\": " << desc_.output.word_count << ",\n";
            mf << "  \"word_bytes\": 16,\n";
            mf << "  \"inputs\": [\n";
            size_t i = 0;
            const size_t ni = desc_.inputs.size();
            for (auto& [name, port] : desc_.inputs) {
                mf << "    {\"name\": \"" << name << "\", \"words\": " << port.word_count
                   << ", \"word_offset\": " << port.word_offset
                   << ", \"byte_base\": " << tensor_region_base(name)
                   << ", \"image_byte_base\": " << image_region_base(name)
                   << ", \"block_bytes\": " << static_cast<uint64_t>(port.word_count) * 16u << "}"
                   << (++i < ni ? "," : "") << "\n";
            }
            mf << "  ]\n";
            mf << "}\n";
        }
    }

    std::cout << "[MatmulNtW4f16LsuOp] Case assembled: " << case_root << " (M=" << m_rows()
              << ", K_total=" << k_total() << ", num_k_blocks=" << num_k_blocks() << ", "
              << static_cast<int>(desc_.num_smc) << " SMCs)" << std::endl;
    return true;
}

bool MatmulNtW4f16LsuOp::assemble_multistage_case() const {
    const std::string case_root = cfg_.case_root;

    // 多 K 块 stage 编排：
    //   MM_K_BATCH=1（旧）：[load(kb), compute(kb)] × nkb + store
    //   MM_K_BATCH=N（默认 4）：[load_batch(N→A/B 连续 + zp/scale 按 group 多槽), compute×N]… + store
    //   M=256（D1' 组=独立 stage）：load_batch(k_batch=2) 后每块发 32 个组 stage，
    //     组 g 复用 M=8 微内核时间轴，仅 a_addr=i*128+g*4、out_addr=g*8（bank2/bank7 行基址）。
    //     B/zp/scale 在该 batch 的 64 个 compute stage 间驻留（weight-stationary）。
    // 累加口径：bank7 跨 compute 保留；Phase B 每块 bank7 += bank2×scale。
    // （N-tile 列窗校验在 assemble_full_case 入口，两条路径共用。）
    const uint32_t nkb = num_k_blocks();
    const uint32_t batch = k_batch_size();
    const uint32_t gsz = std::max<uint32_t>(1u, quant_group_size());
    const std::string smi_root = case_root + "/smi_iq_inst";

    // MM_CASE_IMAGE_ONLY=1：只重建每-SMC HBM 镜像 / golden / meta，跳过 stage 目录发射。
    // FPGA 主机 N-tile 循环用：全部 stage 的 IQ 各 tile 相同（load 槽位地址固定、store 落点
    // 固定，仅镜像内容与 golden 随 tile 变），重建上万个 stage 目录纯属浪费；tile 0 已与
    // RTL 共用一棵 stage 树时尤其如此。
    const bool image_only = [] {
        const char* e = std::getenv("MM_CASE_IMAGE_ONLY");
        return e && e[0] == '1';
    }();

    uint32_t stage_idx = 0u;
    uint32_t num_load_stages = 0u;
    uint32_t num_compute_stages = 0u;
    uint32_t num_epilogue_stages = 0u;
    if (image_only) {
        std::cout << "[MatmulNtW4f16LsuOp] MM_CASE_IMAGE_ONLY=1: skip smi_iq_inst stage emission"
                  << std::endl;
    } else {
        auto store_iqs = generate_store_iq_files();
        if (out_f16() || slot_map_fpga()) {
            // D6：bank7 累加器清零（FPGA 逐 tile/逐次跑时 UR bank7 不会自动清零；RTL 单
            // case 下 ur_data 初始即为 0，此 stage 无副作用）。fp32 输出的 FPGA 槽位图
            // 同样需要（bank7 直接作为输出源，残留会污染累加初值）。
            write_iq_hex_files(smi_root + "/stage_" + std::to_string(stage_idx++),
                               generate_bank7_clear_iq_files(), {}, {});
            ++num_load_stages;
        }
        for (uint32_t kb = 0; kb < nkb; kb += batch) {
            const uint32_t n = std::min(batch, nkb - kb);
            auto load_iqs = generate_load_iq_files_for_k_batch(kb, n);
            write_iq_hex_files(smi_root + "/stage_" + std::to_string(stage_idx++), load_iqs, {}, {});
            ++num_load_stages;
            const uint32_t g0 = (kb * k_chunk()) / gsz;
            for (uint32_t i = 0; i < n; ++i) {
                // 与 load_batch 一致：A/B 各自按本端口 word_count 连续占槽。
                // fp16 下 a_words=4、b_words=16，不可共用 i*a_words 当 b_addr。
                const uint8_t a_addr = static_cast<uint8_t>(i * a_words_per_smc());
                const uint8_t b_addr = static_cast<uint8_t>(i * b_words_per_smc());
                const uint32_t g = ((kb + i) * k_chunk()) / gsz;
                const uint8_t zs_addr = static_cast<uint8_t>((g - g0) * kZpWordsPerSmc);
                if (is_m256()) {
                    // D1'：32 个 row-group 各发一个独立 compute stage（INSTQ_DEPTH=128 放不下
                    // 单 stage 多组）。组 g 的 A 基址 = 块基址 + g*4（M=256 A 打包：组 g 的
                    // 4 个 word 在 SMC 本地 [4g,4g+4)）；bank2/bank7 行基址 = g*8。
                    for (uint32_t grp = 0; grp < op::kMatmulNtW4f16M256Groups; ++grp) {
                        const uint8_t ga_addr =
                            static_cast<uint8_t>(a_addr + grp * 4u);
                        const uint8_t gout_addr = static_cast<uint8_t>(grp * 8u);
                        auto compute_iqs = generate_compute_iq_files_for_k_block(
                            ga_addr, b_addr, kb + i, zs_addr, zs_addr, gout_addr);
                        write_iq_hex_files(smi_root + "/stage_" + std::to_string(stage_idx++), {},
                                           compute_iqs, {});
                        ++num_compute_stages;
                    }
                    continue;
                }
                auto compute_iqs =
                    generate_compute_iq_files_for_k_block(a_addr, b_addr, kb + i, zs_addr, zs_addr);
                write_iq_hex_files(smi_root + "/stage_" + std::to_string(stage_idx++), {},
                                   compute_iqs, {});
                ++num_compute_stages;
            }
        }
        if (out_f16()) {
            // D4：f16 epilogue。bank7 的 M 行 → bank6 的 M/2 个 packed f16 word，按 32 字
            // 一个 stage 切段（INSTQ_DEPTH=128 约束：整段会让 fptofp/rtovr-7 事件数超深；
            // 32 字 → 98 行）。M=8+f16（阶梯 1）：4 字单段。
            constexpr uint32_t kEpWordsPerStage = 32u;
            const uint32_t total_ep_words = desc_.output.word_count;
            for (uint32_t base = 0; base < total_ep_words; base += kEpWordsPerStage) {
                const uint32_t n = std::min(kEpWordsPerStage, total_ep_words - base);
                auto ep_iqs = generate_f16_epilogue_iq_files(base, n);
                write_iq_hex_files(smi_root + "/stage_" + std::to_string(stage_idx++), {}, ep_iqs, {});
                ++num_epilogue_stages;
            }
        }
        write_iq_hex_files(smi_root + "/stage_" + std::to_string(stage_idx++), {}, {}, store_iqs);
    }
    const uint32_t total_stages = stage_idx;

    // ===== 每 SMC：ur_data（真 LDB 场景 bank0/1 起始为 0，scale/zp 作配置预填）+ HBM init + golden =====
    for (uint8_t smc = 0; smc < desc_.num_smc; ++smc) {
        const std::string smc_dir = case_root + "/smc_" + std::to_string(smc);
        mkdir_p(smc_dir + "/ur_data");
        mkdir_p(smc_dir + "/output_bank");

        std::vector<uint8_t> zp_bank0;
        std::vector<uint8_t> scale_bank0;
        std::vector<float> a_tensor;
        std::vector<uint8_t> bq_u4;
        build_test_tensors(smc, cfg_.seed_base, a_tensor, bq_u4, zp_bank0, scale_bank0);

        // bank0/1(A/B) 与 bank4/5(zp/scale) 全部由 stage_0 的 LDB 从 HBM DMA 载入，
        // ur_data 起始一律置 0，以真实验证 load 落地（不再预填 zp/scale）。
        (void)zp_bank0;
        (void)scale_bank0;
        for (int b = 0; b < 8; ++b) {
            const std::string bank_path = smc_dir + "/ur_data/bank" + std::to_string(b) + ".hex";
            write_zero_bank(bank_path, 256);
        }

        std::vector<uint8_t> hbm_data;
        generate_hbm_data(smc, cfg_.seed_base, hbm_data);
        const std::string hbm_path =
            cfg_.hbm_init_dir + "/memory_init" + std::to_string(smc + 1) + ".hex";
        write_hex_file(hbm_path, hbm_data, 16);

        std::vector<uint8_t> golden;
        generate_golden_for_smc(smc, {}, golden);
        if (golden.size() / 16 < 256u) golden.resize(256u * 16u, 0u);
        write_hex_file(
            smc_dir + "/output_bank/bank" + std::to_string(desc_.output.bank) + "_final.hex",
            golden, 16);
    }

    // meta_v3.json：供 verify_ddr_v3.py 定位 store 落点比对 golden。
    {
        std::ofstream mf(case_root + "/meta_v3.json");
        if (mf) {
            mf << "{\n";
            mf << "  \"op\": \"" << get_name() << "\",\n";
            mf << "  \"mode\": \"multistage\",\n";
            mf << "  \"num_smc\": " << static_cast<int>(desc_.num_smc) << ",\n";
            mf << "  \"m_rows\": " << m_rows() << ",\n";
            mf << "  \"k_total\": " << k_total() << ",\n";
            mf << "  \"k_chunk\": " << k_chunk() << ",\n";
            mf << "  \"num_k_blocks\": " << num_k_blocks() << ",\n";
            mf << "  \"k_batch\": " << batch << ",\n";
            mf << "  \"n_total\": " << n_total() << ",\n";
            mf << "  \"n_tile_idx\": " << n_tile_idx() << ",\n";
            mf << "  \"col_base\": " << col_base() << ",\n";
            mf << "  \"slot_map\": \"" << (slot_map_fpga() ? "fpga" : "rtl") << "\",\n";
            if (slot_map_fpga()) {
                // FPGA 固定全局图（2026-09-30）：packer 按此把镜像切片打包成槽内单拷贝。
                mf << "  \"hbm_layout\": \"global_slots\",\n";
                mf << "  \"slice_stride\": " << desc_.gr_intlv_addr << ",\n";
                mf << "  \"zero_byte_base\": " << zero_region_base() << ",\n";
            }
            mf << "  \"layout\": \"tensor_major\",\n";
            mf << "  \"store_byte_base\": " << store_gr_base() << ",\n";
            mf << "  \"out_bank\": " << static_cast<int>(desc_.output.bank) << ",\n";
            mf << "  \"out_words\": " << desc_.output.word_count << ",\n";
            // f16 packed 输出（M=256 恒开 / M=8 由 MM_OUT_DTYPE=f16 开）：verify 按此走 f16 分支。
            mf << "  \"out_dtype\": \"" << (out_f16() ? "f16" : "f32") << "\",\n";
            mf << "  \"gr_intlv_addr\": " << (desc_.store_gr_intlv_addr ? desc_.store_gr_intlv_addr : desc_.gr_intlv_addr) << ",\n";
            mf << "  \"has_epilogue\": " << (out_f16() ? "true" : "false") << ",\n";
            mf << "  \"word_bytes\": 16,\n";
            mf << "  \"inputs\": [\n";
            {
                size_t i = 0;
                const size_t ni = desc_.inputs.size();
                for (auto& [name, port] : desc_.inputs) {
                    mf << "    {\"name\": \"" << name << "\", \"words\": " << port.word_count
                       << ", \"word_offset\": " << port.word_offset
                       << ", \"byte_base\": " << tensor_region_base(name)
                       << ", \"image_byte_base\": " << image_region_base(name)
                       << ", \"block_bytes\": " << static_cast<uint64_t>(port.word_count) * 16u
                       << "}" << (++i < ni ? "," : "") << "\n";
                }
            }
            mf << "  ]\n";
            mf << "}\n";
        }
    }

    std::cout << "[MatmulNtW4f16LsuOp] Multistage case assembled: " << case_root
              << " (stages=" << total_stages << " = " << num_load_stages << "×load_batch + "
              << num_compute_stages << "×compute + " << num_epilogue_stages
              << "×epilogue + store; M=" << m_rows()
              << ", K_total=" << k_total() << ", num_k_blocks=" << nkb << ", k_batch=" << batch
              << ", " << static_cast<int>(desc_.num_smc) << " SMCs)" << std::endl;
    return true;
}

}  // namespace lsu
}  // namespace aiacc
