#include "mininpuc/sim/Sim.h"

#include "mininpuc/Float16.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace mininpu {

// FP16 转换见 lib/runtime/Float16.cpp(编译器侧 V_RMS eps 编码共用)
static float f16ToF32(uint16_t h) { return f16BitsToF32(h); }
static uint16_t f32ToF16(float x) { return f32ToF16Bits(x); }

// ---- Profiler ----
void Profiler::add(const std::string& name, const std::string& pid, int64_t start,
                   int64_t dur, uint64_t bytes, uint64_t flops) {
  events_.push_back({name, pid, start, dur, bytes, flops});
}

void Profiler::writeChromeTrace(const std::string& path) const {
  std::ofstream os(path);
  if (!os) throw std::runtime_error("profiler: cannot open " + path);
  os << "{\"traceEvents\":[\n";
  bool first = true;
  for (const auto& e : events_) {
    if (!first) os << ",\n";
    first = false;
    // §11.3:1 cycle = 1 任意单位,图上自洽
    os << "{\"name\":\"" << e.name << "\",\"cat\":\"" << e.pid
       << "\",\"ph\":\"X\",\"ts\":" << e.start << ",\"dur\":" << e.dur
       << ",\"pid\":\"" << e.pid << "\",\"tid\":0";
    // roofline 口径(§13 步骤 10):实际 DMA 字节 / GEMM FLOPs 随事件落盘
    if (e.bytes || e.flops)
      os << ",\"args\":{\"bytes\":" << e.bytes << ",\"flops\":" << e.flops << "}";
    os << "}";
  }
  os << "\n]}\n";
}

// ---- Device ----
uint64_t Device::allocDram(uint64_t bytes, uint64_t align) {
  uint64_t base = (dramBump_ + align - 1) / align * align;
  uint64_t end = base + bytes;
  if (end > dram_.size()) dram_.resize(end);
  dramBump_ = end;
  return base;
}

// ---- Executor ----
const Descriptor& Executor::desc(uint8_t id) const {
  const Descriptor* d = exe_.findDesc(id);
  if (!d)
    throw std::runtime_error("sim: unknown descriptor id " + std::to_string(id));
  return *d;
}

void Executor::schedule(int ch, const char* name, int64_t cycles, uint64_t bytes,
                        uint64_t flops) {
  int64_t start = now_ > chFree_[ch] ? now_ : chFree_[ch];
  int64_t done = start + cycles;
  chFree_[ch] = done;
  if (prof_) prof_->add(name, ch == CH_DMA_LD ? "dma_ld" : ch == CH_DMA_ST ? "dma_st"
                            : ch == CH_GEMM ? "gemm" : "vec", start, cycles, bytes, flops);
}

static void checkBounds(const std::vector<uint8_t>& buf, const Descriptor& d,
                        uint64_t offset, const char* what) {
  if (offset + d.bytes > buf.size())
    throw std::runtime_error(std::string("sim: ") + what + " 越界: desc " +
                             std::to_string(d.id) + " offset " + std::to_string(offset) +
                             " + bytes " + std::to_string(d.bytes) + " > 容量 " +
                             std::to_string(buf.size()));
}

static float readElem(const std::vector<uint8_t>& buf, const Descriptor& d,
                      const int32_t* idx) {
  int64_t off = 0;
  for (int r = 0; r < d.rank; ++r) off += static_cast<int64_t>(idx[r]) * d.stride[r];
  off = static_cast<int64_t>(d.base) + off * dtypeSize(static_cast<DType>(d.dtype));
  if (d.dtype == DT_F16) {
    uint16_t h;
    std::memcpy(&h, buf.data() + off, 2);
    return f16ToF32(h);
  }
  if (d.dtype == DT_BF16) { // 三期:高 16 bit 截断,上转无损(§6.6)
    uint16_t b;
    std::memcpy(&b, buf.data() + off, 2);
    return bf16BitsToF32(b);
  }
  float f;
  std::memcpy(&f, buf.data() + off, 4);
  return f;
}

static void writeElem(std::vector<uint8_t>& buf, const Descriptor& d,
                      const int32_t* idx, float v) {
  int64_t off = 0;
  for (int r = 0; r < d.rank; ++r) off += static_cast<int64_t>(idx[r]) * d.stride[r];
  off = static_cast<int64_t>(d.base) + off * dtypeSize(static_cast<DType>(d.dtype));
  if (d.dtype == DT_F16) {
    uint16_t h = f32ToF16(v);
    std::memcpy(buf.data() + off, &h, 2);
  } else if (d.dtype == DT_BF16) {
    uint16_t b = f32ToBf16Bits(v);
    std::memcpy(buf.data() + off, &b, 2);
  } else {
    std::memcpy(buf.data() + off, &v, 4);
  }
}

// 三期-FP8(§6.6):e4m3 容器读写。读取经 f8E4M3BitsToF32 上转——e4m3×e4m3
// 乘积(≤8 位有效尾数)在 f32 精确可表示,GEMM_F8 的累加误差只有 f32 RNE;
// 写入为位模式直存(饱和/NaN 语义在 f32ToF8E4M3Bits 内)。
static float readElemF8(const std::vector<uint8_t>& buf, const Descriptor& d,
                        const int32_t* idx) {
  int64_t off = 0;
  for (int r = 0; r < d.rank; ++r) off += static_cast<int64_t>(idx[r]) * d.stride[r];
  off = static_cast<int64_t>(d.base) + off * dtypeSize(static_cast<DType>(d.dtype));
  uint8_t q;
  std::memcpy(&q, buf.data() + off, 1);
  return f8E4M3BitsToF32(q);
}

static void writeElemF8(std::vector<uint8_t>& buf, const Descriptor& d,
                        const int32_t* idx, float v) {
  int64_t off = 0;
  for (int r = 0; r < d.rank; ++r) off += static_cast<int64_t>(idx[r]) * d.stride[r];
  off = static_cast<int64_t>(d.base) + off * dtypeSize(static_cast<DType>(d.dtype));
  uint8_t q = f32ToF8E4M3Bits(v);
  std::memcpy(buf.data() + off, &q, 1);
}

// 二期-INT8:整数 dtype 走独立精确路径(DT_I8 符号扩展;DT_I32 直读/直写),
// 不经过 float,保证 GEMM_I8 的 i32 跨 tile 累加 bit-exact(§6.5)。
static int32_t readElemI32(const std::vector<uint8_t>& buf, const Descriptor& d,
                           const int32_t* idx) {
  int64_t off = 0;
  for (int r = 0; r < d.rank; ++r) off += static_cast<int64_t>(idx[r]) * d.stride[r];
  off = static_cast<int64_t>(d.base) + off * dtypeSize(static_cast<DType>(d.dtype));
  if (d.dtype == DT_I8) {
    int8_t v;
    std::memcpy(&v, buf.data() + off, 1);
    return static_cast<int32_t>(v);
  }
  int32_t v;
  std::memcpy(&v, buf.data() + off, 4);
  return v;
}

static void writeElemI32(std::vector<uint8_t>& buf, const Descriptor& d,
                         const int32_t* idx, int32_t v) {
  int64_t off = 0;
  for (int r = 0; r < d.rank; ++r) off += static_cast<int64_t>(idx[r]) * d.stride[r];
  off = static_cast<int64_t>(d.base) + off * dtypeSize(static_cast<DType>(d.dtype));
  if (d.dtype == DT_I8) {
    int8_t s = static_cast<int8_t>(v);
    std::memcpy(buf.data() + off, &s, 1);
  } else {
    std::memcpy(buf.data() + off, &v, 4);
  }
}

void Executor::execDma(const Descriptor& src, const Descriptor& dst, uint32_t byteOff,
                       bool load) {
  if (src.dtype != dst.dtype)
    throw std::runtime_error("sim: DMA dtype mismatch: desc " + std::to_string(src.id) +
                             " vs " + std::to_string(dst.id));
  if (src.rank != dst.rank)
    throw std::runtime_error("sim: DMA rank mismatch");
  const Descriptor& slice = load ? dst : src;   // sram 侧(切片几何)
  const Descriptor& whole = load ? src : dst;   // dram 侧(提供 stride)
  if ((whole.base + byteOff) % 32 != 0)
    throw std::runtime_error("sim: 未对齐 DMA: dram base+imm 非 32B 对齐 (desc " +
                             std::to_string(whole.id) + ")");
  uint32_t es = dtypeSize(static_cast<DType>(slice.dtype));
  if ((int64_t(slice.shape[slice.rank - 1]) * es) % 32 != 0)
    throw std::runtime_error("sim: 未对齐 DMA: inner size 非 32B 对齐 (desc " +
                             std::to_string(slice.id) + ")");

  auto& srcBuf = load ? dev_.dram() : dev_.sram();
  auto& dstBuf = load ? dev_.sram() : dev_.dram();
  // 边界检查:按 slice 形状 + 各自 stride 计算包围盒末端(而非整块 bytes,
  // 否则带 imm 偏移的切片会误报越界)
  {
    int64_t dramLast = byteOff, sramLast = 0;
    for (int r = 0; r < slice.rank; ++r) {
      dramLast += static_cast<int64_t>(slice.shape[r] - 1) * whole.stride[r] * es;
      sramLast += static_cast<int64_t>(slice.shape[r] - 1) * slice.stride[r] * es;
    }
    auto& srcLast = load ? dramLast : sramLast;
    auto& dstLast = load ? sramLast : dramLast;
    if (static_cast<int64_t>(load ? whole.base : slice.base) + srcLast + es >
        srcBuf.size())
      throw std::runtime_error("sim: DMA 源越界: desc " +
                               std::to_string(load ? whole.id : slice.id) +
                               " +imm " + std::to_string(byteOff));
    if (static_cast<int64_t>(load ? slice.base : whole.base) + dstLast + es >
        dstBuf.size())
      throw std::runtime_error("sim: DMA 目的越界: desc " +
                               std::to_string(load ? slice.id : whole.id) +
                               "(SRAM 容量 " + std::to_string(dstBuf.size()) + ")");
  }

  // 通用 N 维拷贝:形状取切片;DRAM 侧地址 = base + imm + Σ idx*stride*es,
  // SRAM 侧地址 = base + Σ idx*stride*es
  int32_t idx[4] = {0, 0, 0, 0};
  uint64_t total = 1;
  for (int r = 0; r < slice.rank; ++r) total *= slice.shape[r];
  for (uint64_t t = 0; t < total; ++t) {
    uint64_t rem = t;
    for (int r = slice.rank - 1; r >= 0; --r) {
      idx[r] = static_cast<int32_t>(rem % slice.shape[r]);
      rem /= slice.shape[r];
    }
    int64_t dramOff = static_cast<int64_t>(whole.base) + byteOff;
    int64_t sramOff = static_cast<int64_t>(slice.base);
    for (int r = 0; r < slice.rank; ++r) {
      dramOff += static_cast<int64_t>(idx[r]) * whole.stride[r] * es;
      sramOff += static_cast<int64_t>(idx[r]) * slice.stride[r] * es;
    }
    int64_t srcOff = load ? dramOff : sramOff;
    int64_t dstOff = load ? sramOff : dramOff;
    if (srcOff + es > srcBuf.size())
      throw std::runtime_error("sim: DMA 源越界 desc " + std::to_string(whole.id));
    if (dstOff + es > dstBuf.size())
      throw std::runtime_error("sim: DMA 目的越界 desc " + std::to_string(slice.id));
    std::memcpy(dstBuf.data() + dstOff, srcBuf.data() + srcOff, es);
  }
  dmaBytes_ += slice.bytes;
}

void Executor::execGemm(uint64_t w0, uint64_t w1) {
  const Descriptor& C = desc(inDst(w0));
  const Descriptor& A = desc(inSrc0(w0));
  const Descriptor& B = desc(inSrc1(w0));
  uint16_t accMode = static_cast<uint16_t>((w1 >> 40) & 0xff);
  if (C.memspace != MS_SRAM || A.memspace != MS_SRAM || B.memspace != MS_SRAM)
    throw std::runtime_error("sim: GEMM 操作数必须都在 SRAM");
  uint32_t Tm = A.shape[0], Tk = A.shape[1];
  if (B.shape[0] != Tk || C.shape[0] != Tm || B.shape[1] != C.shape[1])
    throw std::runtime_error("sim: GEMM 形状不一致");
  uint32_t Tn = B.shape[1];

  checkBounds(dev_.sram(), A, 0, "GEMM A");
  checkBounds(dev_.sram(), B, 0, "GEMM B");
  checkBounds(dev_.sram(), C, 0, "GEMM C");

  auto& sram = dev_.sram();
  for (uint32_t m = 0; m < Tm; ++m) {
    for (uint32_t n = 0; n < Tn; ++n) {
      int32_t idxC[4] = {static_cast<int32_t>(m), static_cast<int32_t>(n), 0, 0};
      float acc = accMode == 1 ? readElem(sram, C, idxC) : 0.0f;
      for (uint32_t k = 0; k < Tk; ++k) {
        int32_t idxA[4] = {static_cast<int32_t>(m), static_cast<int32_t>(k), 0, 0};
        int32_t idxB[4] = {static_cast<int32_t>(k), static_cast<int32_t>(n), 0, 0};
        acc += readElem(sram, A, idxA) * readElem(sram, B, idxB);
      }
      writeElem(sram, C, idxC, acc);
    }
  }
}

// 二期-INT8(§6.5):A/B 为 DT_I8,C 为 DT_I32;跨 Tk tile 的 acc 走 i32 精确
// 整数累加,|acc| ≤ K*127²(K=4096 时 66M ≪ 2³¹),与 f16 C 块的部分和量化
// 形成对照——INT8 路径的数值误差全部集中在最后一次 dequant(§2.3)。
void Executor::execGemmI8(uint64_t w0, uint64_t w1) {
  const Descriptor& C = desc(inDst(w0));
  const Descriptor& A = desc(inSrc0(w0));
  const Descriptor& B = desc(inSrc1(w0));
  uint16_t accMode = static_cast<uint16_t>((w1 >> 40) & 0xff);
  if (C.memspace != MS_SRAM || A.memspace != MS_SRAM || B.memspace != MS_SRAM)
    throw std::runtime_error("sim: GEMM_I8 操作数必须都在 SRAM");
  if (A.dtype != DT_I8 || B.dtype != DT_I8 || C.dtype != DT_I32)
    throw std::runtime_error("sim: GEMM_I8 要求 A/B=DT_I8、C=DT_I32");
  uint32_t Tm = A.shape[0], Tk = A.shape[1];
  if (B.shape[0] != Tk || C.shape[0] != Tm || B.shape[1] != C.shape[1])
    throw std::runtime_error("sim: GEMM_I8 形状不一致");
  uint32_t Tn = B.shape[1];

  checkBounds(dev_.sram(), A, 0, "GEMM_I8 A");
  checkBounds(dev_.sram(), B, 0, "GEMM_I8 B");
  checkBounds(dev_.sram(), C, 0, "GEMM_I8 C");

  auto& sram = dev_.sram();
  for (uint32_t m = 0; m < Tm; ++m) {
    for (uint32_t n = 0; n < Tn; ++n) {
      int32_t idxC[4] = {static_cast<int32_t>(m), static_cast<int32_t>(n), 0, 0};
      int32_t acc = accMode == 1 ? readElemI32(sram, C, idxC) : 0;
      for (uint32_t k = 0; k < Tk; ++k) {
        int32_t idxA[4] = {static_cast<int32_t>(m), static_cast<int32_t>(k), 0, 0};
        int32_t idxB[4] = {static_cast<int32_t>(k), static_cast<int32_t>(n), 0, 0};
        acc += readElemI32(sram, A, idxA) * readElemI32(sram, B, idxB);
      }
      writeElemI32(sram, C, idxC, acc);
    }
  }
}

// 三期-FP8(§6.6):A/B 为 DT_F8E4M3,C 为 DT_F32——K·448²(K=256 时 5.1e7)
// 溢出 f16 上限 65504,f32 累加是 H100 WGMMA 口径。e4m3×e4m3 乘积(≤8 位
// 有效尾数)在 f32 精确可表示,故累加误差只有 f32 RNE 舍入(~ulp 级),
// 与 GEMM_I8 的 i32 bit-exact、OP_GEMM 的 f16 部分和量化形成三档对照(§2.3)。
void Executor::execGemmF8(uint64_t w0, uint64_t w1) {
  const Descriptor& C = desc(inDst(w0));
  const Descriptor& A = desc(inSrc0(w0));
  const Descriptor& B = desc(inSrc1(w0));
  uint16_t accMode = static_cast<uint16_t>((w1 >> 40) & 0xff);
  if (C.memspace != MS_SRAM || A.memspace != MS_SRAM || B.memspace != MS_SRAM)
    throw std::runtime_error("sim: GEMM_F8 操作数必须都在 SRAM");
  if (A.dtype != DT_F8E4M3 || B.dtype != DT_F8E4M3 || C.dtype != DT_F32)
    throw std::runtime_error("sim: GEMM_F8 要求 A/B=DT_F8E4M3、C=DT_F32");
  uint32_t Tm = A.shape[0], Tk = A.shape[1];
  if (B.shape[0] != Tk || C.shape[0] != Tm || B.shape[1] != C.shape[1])
    throw std::runtime_error("sim: GEMM_F8 形状不一致");
  uint32_t Tn = B.shape[1];

  checkBounds(dev_.sram(), A, 0, "GEMM_F8 A");
  checkBounds(dev_.sram(), B, 0, "GEMM_F8 B");
  checkBounds(dev_.sram(), C, 0, "GEMM_F8 C");

  auto& sram = dev_.sram();
  for (uint32_t m = 0; m < Tm; ++m) {
    for (uint32_t n = 0; n < Tn; ++n) {
      int32_t idxC[4] = {static_cast<int32_t>(m), static_cast<int32_t>(n), 0, 0};
      float acc = accMode == 1 ? readElem(sram, C, idxC) : 0.0f;
      for (uint32_t k = 0; k < Tk; ++k) {
        int32_t idxA[4] = {static_cast<int32_t>(m), static_cast<int32_t>(k), 0, 0};
        int32_t idxB[4] = {static_cast<int32_t>(k), static_cast<int32_t>(n), 0, 0};
        acc += readElemF8(sram, A, idxA) * readElemF8(sram, B, idxB);
      }
      writeElem(sram, C, idxC, acc);
    }
  }
}

// V_QUANT(§6.5/§6.6):容器 dtype 分派——dst=DT_I8 时 y[m,n]=sat_RNE(x/sa[m])
// (对称域 [-127,127],-128 保留避免 |min|≠|max| 偏置);dst=DT_F8E4M3 时
// y[m,n]=e4m3_sat(x/sa[m])(RNE 尾数、饱和 ±448、subnormal 到 2⁻⁹)。
// sa 为 1-D:长度=行数(per-token)或 1(per-tensor,FP8 标准粒度,广播)。
void Executor::execVecQuant(uint8_t dstId, uint8_t srcId, uint8_t saId) {
  const Descriptor& dst = desc(dstId);
  const Descriptor& src = desc(srcId);
  const Descriptor& sa = desc(saId);
  if (src.memspace != MS_SRAM || dst.memspace != MS_SRAM || sa.memspace != MS_SRAM)
    throw std::runtime_error("sim: V_QUANT 操作数必须在 SRAM");
  if (dst.dtype != DT_I8 && dst.dtype != DT_F8E4M3)
    throw std::runtime_error("sim: V_QUANT 输出必须 DT_I8 或 DT_F8E4M3");
  if (src.rank != 2 || dst.rank != 2 || sa.rank != 1)
    throw std::runtime_error("sim: V_QUANT 要求 rank-2 输入 + 1-D 行 scale");
  uint32_t rows = src.shape[0], cols = src.shape[1];
  if (dst.shape[0] != rows || dst.shape[1] != cols ||
      (sa.shape[0] != rows && sa.shape[0] != 1))
    throw std::runtime_error("sim: V_QUANT 形状不一致(sa 长度须=行数或 1)");
  auto& sram = dev_.sram();
  for (uint32_t m = 0; m < rows; ++m) {
    // per-tensor scale(len-1)时行索引恒取 0,即广播
    int32_t idxS[4] = {static_cast<int32_t>(sa.shape[0] == 1 ? 0 : m), 0, 0, 0};
    float inv = 1.0f / readElem(sram, sa, idxS);
    for (uint32_t n = 0; n < cols; ++n) {
      int32_t idx[4] = {static_cast<int32_t>(m), static_cast<int32_t>(n), 0, 0};
      float q = readElem(sram, src, idx) * inv;
      if (dst.dtype == DT_I8) {
        q = std::nearbyint(q);
        if (q > 127.0f) q = 127.0f;
        if (q < -127.0f) q = -127.0f;
        writeElemI32(sram, dst, idx, static_cast<int32_t>(q));
      } else {
        writeElemF8(sram, dst, idx, q);
      }
    }
  }
}

// V_DEQUANT(§6.5/§6.6):y[m,n] = x[m,n] * sb[n] × sa[m]。源 dtype 分派:
// DT_I32(GEMM_I8 积分路径,精确整数)或 DT_F32(GEMM_F8 路径);目标
// DT_F16 或 DT_BF16(readElem/writeElem 已分派)。sb 长度=末维或 1
// (per-tensor),sa 长度=行数或 1(sa 经 imm_hi 低 8 bit 的第 4 描述符)。
void Executor::execVecDequant(uint8_t dstId, uint8_t srcId, uint8_t sbId,
                              uint8_t saId) {
  const Descriptor& dst = desc(dstId);
  const Descriptor& src = desc(srcId);
  const Descriptor& sb = desc(sbId);
  const Descriptor& sa = desc(saId);
  if (src.memspace != MS_SRAM || dst.memspace != MS_SRAM ||
      sb.memspace != MS_SRAM || sa.memspace != MS_SRAM)
    throw std::runtime_error("sim: V_DEQUANT 操作数必须在 SRAM");
  if (src.dtype != DT_I32 && src.dtype != DT_F32)
    throw std::runtime_error("sim: V_DEQUANT 要求 x=DT_I32 或 DT_F32");
  if (dst.dtype != DT_F16 && dst.dtype != DT_BF16)
    throw std::runtime_error("sim: V_DEQUANT 要求 y=DT_F16 或 DT_BF16");
  if (src.rank != 2 || dst.rank != 2 || sb.rank != 1 || sa.rank != 1)
    throw std::runtime_error("sim: V_DEQUANT 要求 rank-2 + 两个 1-D scale");
  uint32_t rows = src.shape[0], cols = src.shape[1];
  if (dst.shape[0] != rows || dst.shape[1] != cols ||
      (sb.shape[0] != cols && sb.shape[0] != 1) ||
      (sa.shape[0] != rows && sa.shape[0] != 1))
    throw std::runtime_error("sim: V_DEQUANT 形状不一致(sb=末维或 1,sa=行数或 1)");
  auto& sram = dev_.sram();
  for (uint32_t m = 0; m < rows; ++m) {
    int32_t idxS[4] = {static_cast<int32_t>(sa.shape[0] == 1 ? 0 : m), 0, 0, 0};
    float saV = readElem(sram, sa, idxS);
    for (uint32_t n = 0; n < cols; ++n) {
      int32_t idx[4] = {static_cast<int32_t>(m), static_cast<int32_t>(n), 0, 0};
      int32_t idxB[4] = {static_cast<int32_t>(sb.shape[0] == 1 ? 0 : n), 0, 0, 0};
      float xv = src.dtype == DT_I32
                     ? static_cast<float>(readElemI32(sram, src, idx))
                     : readElem(sram, src, idx);
      float v = xv * readElem(sram, sb, idxB) * saV;
      writeElem(sram, dst, idx, v);
    }
  }
}

void Executor::execVecRms(uint8_t dstId, uint8_t srcId, uint8_t gammaId, uint16_t epsBits) {  const Descriptor& dst = desc(dstId);
  const Descriptor& src = desc(srcId);
  const Descriptor& gamma = desc(gammaId);
  if (src.memspace != MS_SRAM || dst.memspace != MS_SRAM)
    throw std::runtime_error("sim: V_RMS 操作数必须在 SRAM");
  uint32_t H = src.shape[src.rank - 1];
  if (gamma.shape[0] != H || dst.shape[dst.rank - 1] != H)
    throw std::runtime_error("sim: V_RMS gamma/输出形状不一致");
  // eps imm 按 dst elem dtype 解码(§6.6):bf16 图由 CodeGen 编 bf16 位,
  // f16/f32 图沿现状编 f16 位(f32 图 eps 只有 f16 精度,已记录在案)
  float eps =
      dst.dtype == DT_BF16 ? bf16BitsToF32(epsBits) : f16ToF32(epsBits);
  auto& sram = dev_.sram();
  uint64_t rows = 1;
  for (int r = 0; r + 1 < src.rank; ++r) rows *= src.shape[r];
  for (uint64_t row = 0; row < rows; ++row) {
    float ms = 0.0f;
    for (uint32_t i = 0; i < H; ++i) {
      int32_t idx[4] = {static_cast<int32_t>(row), static_cast<int32_t>(i), 0, 0};
      float x = readElem(sram, src, idx);
      ms += x * x;
    }
    ms /= H;
    float scale = 1.0f / std::sqrt(ms + eps);
    for (uint32_t i = 0; i < H; ++i) {
      int32_t idx[4] = {static_cast<int32_t>(row), static_cast<int32_t>(i), 0, 0};
      int32_t idxG[4] = {static_cast<int32_t>(i), 0, 0, 0};
      float x = readElem(sram, src, idx);
      float g = readElem(sram, gamma, idxG);
      writeElem(sram, dst, idx, x * scale * g);
    }
  }
}

void Executor::execVecEltwise(uint8_t op, uint8_t dstId, uint8_t s0Id, uint8_t s1Id) {
  const Descriptor& dst = desc(dstId);
  const Descriptor& s0 = desc(s0Id);
  if (dst.memspace != MS_SRAM || s0.memspace != MS_SRAM)
    throw std::runtime_error("sim: vec op 操作数必须在 SRAM");
  uint64_t numel = 1;
  for (int r = 0; r < dst.rank; ++r) numel *= dst.shape[r];
  const Descriptor* s1 = (op == OP_V_MUL || op == OP_V_ADD) ? &desc(s1Id) : nullptr;
  if (s1) {
    if (s1->memspace != MS_SRAM)
      throw std::runtime_error("sim: vec op 操作数必须在 SRAM");
    // rhs 支持同形,或长度=末维的 1-D bias 广播(§8.1,matmul bias epilogue)
    bool sameShape = s1->rank == dst.rank;
    for (int r = 0; sameShape && r < dst.rank; ++r)
      sameShape = s1->shape[r] == dst.shape[r];
    if (!sameShape &&
        !(s1->rank == 1 && s1->shape[0] == dst.shape[dst.rank - 1]))
      throw std::runtime_error("sim: vec rhs 须与 lhs 同形或为长度=末维的 1-D");
  }
  auto& sram = dev_.sram();
  for (uint64_t t = 0; t < numel; ++t) {
    int32_t idx[4] = {0, 0, 0, 0};
    uint64_t rem = t;
    for (int r = dst.rank - 1; r >= 0; --r) {
      idx[r] = static_cast<int32_t>(rem % dst.shape[r]);
      rem /= dst.shape[r];
    }
    float a = readElem(sram, s0, idx);
    float out;
    if (op == OP_V_SILU) out = a / (1.0f + std::exp(-a));
    else {
      float b;
      if (s1->rank == 1 && s1->rank != dst.rank) { // 1-D bias 广播
        int32_t idxB[4] = {idx[dst.rank - 1], 0, 0, 0};
        b = readElem(sram, *s1, idxB);
      } else {
        b = readElem(sram, *s1, idx);
      }
      out = (op == OP_V_MUL) ? a * b : a + b;
    }
    writeElem(sram, dst, idx, out);
  }
}

void Executor::run() {
  const auto& spec = dev_.spec();
  size_t pc = 0;
  bool ended = false;
  while (pc < exe_.code.size() && !ended) {
    uint64_t w = exe_.code[pc++];
    switch (inOp(w)) {
    case OP_DMA_LD:
    case OP_DMA_ST: {
      bool load = inOp(w) == OP_DMA_LD;
      const Descriptor& src = desc(inSrc0(w));
      const Descriptor& dst = desc(inDst(w));
      uint32_t byteOff = inImm(w);
      if (load && (src.memspace != MS_DRAM || dst.memspace != MS_SRAM))
        throw std::runtime_error("sim: DMA_LD 必须 DRAM→SRAM");
      if (!load && (src.memspace != MS_SRAM || dst.memspace != MS_DRAM))
        throw std::runtime_error("sim: DMA_ST 必须 SRAM→DRAM");
      execDma(src, dst, byteOff, load);
      const Descriptor& slice = load ? dst : src;
      int64_t cycles = static_cast<int64_t>(spec.dmaLatencyCycles) +
          (static_cast<int64_t>(slice.bytes) +
           static_cast<int64_t>(spec.dmaBytesPerCycle()) - 1) /
              static_cast<int64_t>(spec.dmaBytesPerCycle());
      schedule(load ? CH_DMA_LD : CH_DMA_ST, load ? "DMA_LD" : "DMA_ST", cycles,
               slice.bytes);
      break;
    }
    case OP_GEMM: {
      if (pc >= exe_.code.size()) throw std::runtime_error("sim: GEMM 缺第二字");
      uint64_t w1 = exe_.code[pc++];
      execGemm(w, w1);
      const Descriptor& A = desc(inSrc0(w));
      const Descriptor& B = desc(inSrc1(w));
      uint64_t macs = uint64_t(A.shape[0]) * A.shape[1] * B.shape[1];
      int64_t cycles = static_cast<int64_t>(
          (macs + spec.macsPerCycle - 1) / spec.macsPerCycle);
      schedule(CH_GEMM, "GEMM", cycles, 0, 2 * macs);
      break;
    }
    case OP_GEMM_I8: {
      if (pc >= exe_.code.size()) throw std::runtime_error("sim: GEMM_I8 缺第二字");
      uint64_t w1 = exe_.code[pc++];
      execGemmI8(w, w1);
      const Descriptor& A = desc(inSrc0(w));
      const Descriptor& B = desc(inSrc1(w));
      uint64_t macs = uint64_t(A.shape[0]) * A.shape[1] * B.shape[1];
      int64_t cycles = static_cast<int64_t>(
          (macs + spec.macsPerCycleInt8 - 1) / spec.macsPerCycleInt8);
      schedule(CH_GEMM, "GEMM_I8", cycles, 0, 2 * macs);
      break;
    }
    case OP_GEMM_F8: { // 三期-FP8:峰值 macs_per_cycle_fp8(默认 2× f16,§6.6)
      if (pc >= exe_.code.size()) throw std::runtime_error("sim: GEMM_F8 缺第二字");
      uint64_t w1 = exe_.code[pc++];
      execGemmF8(w, w1);
      const Descriptor& A = desc(inSrc0(w));
      const Descriptor& B = desc(inSrc1(w));
      uint64_t macs = uint64_t(A.shape[0]) * A.shape[1] * B.shape[1];
      int64_t cycles = static_cast<int64_t>(
          (macs + spec.macsPerCycleFp8 - 1) / spec.macsPerCycleFp8);
      schedule(CH_GEMM, "GEMM_F8", cycles, 0, 2 * macs);
      break;
    }
    case OP_V_QUANT:
    case OP_V_DEQUANT: {
      if (inOp(w) == OP_V_QUANT) {
        execVecQuant(inDst(w), inSrc0(w), inSrc1(w));
      } else {
        // 第 4 操作数 sa 由 imm_hi 低 8 bit 携带(§6.5 编码惯例)
        execVecDequant(inDst(w), inSrc0(w), inSrc1(w),
                       static_cast<uint8_t>(inImmHi(w) & 0xff));
      }
      const Descriptor& dst = desc(inDst(w));
      int64_t cycles = (static_cast<int64_t>(dst.bytes) +
                        static_cast<int64_t>(spec.vecBwBytesPerCycle) - 1) /
                       static_cast<int64_t>(spec.vecBwBytesPerCycle);
      schedule(CH_VEC, inOp(w) == OP_V_QUANT ? "V_QUANT" : "V_DEQUANT", cycles,
               dst.bytes);
      break;
    }
    case OP_V_RMS: {
      execVecRms(inDst(w), inSrc0(w), inSrc1(w), inImmLo(w));
      const Descriptor& src = desc(inSrc0(w));
      uint64_t rows = 1;
      for (int r = 0; r + 1 < src.rank; ++r) rows *= src.shape[r];
      int64_t cycles = static_cast<int64_t>(spec.vecRmsAlpha) *
                       static_cast<int64_t>(src.shape[src.rank - 1]) *
                       static_cast<int64_t>(rows);
      // roofline 口径与 eltwise 一致:payload 取 dst 字节(读/写经 vec 引擎)
      schedule(CH_VEC, "V_RMS", cycles, desc(inDst(w)).bytes);
      break;
    }
    case OP_V_SILU:
    case OP_V_MUL:
    case OP_V_ADD: {
      execVecEltwise(inOp(w), inDst(w), inSrc0(w), inSrc1(w));
      const Descriptor& dst = desc(inDst(w));
      int64_t cycles = (static_cast<int64_t>(dst.bytes) +
                        static_cast<int64_t>(spec.vecBwBytesPerCycle) - 1) /
                       static_cast<int64_t>(spec.vecBwBytesPerCycle);
      schedule(CH_VEC, inOp(w) == OP_V_SILU ? "V_SILU" : inOp(w) == OP_V_MUL ? "V_MUL"
                                                                             : "V_ADD",
               cycles, dst.bytes);
      break;
    }
    case OP_BAR: {
      uint16_t mask = static_cast<uint16_t>(inImmLo(w) & 0xf);
      int64_t target = now_;
      for (int ch = 0; ch < 4; ++ch)
        if (mask & chMask(ch)) target = std::max(target, chFree_[ch]);
      if (prof_ && target > now_)
        prof_->add("BAR", "host", now_, target - now_);
      now_ = target;
      break;
    }
    case OP_END:
      ended = true;
      break;
    default:
      throw std::runtime_error("sim: unknown opcode 0x" +
                               std::to_string(static_cast<int>(inOp(w))));
    }
    if (now_ > (int64_t)1e12 || chFree_[CH_GEMM] > (int64_t)1e12)
      throw std::runtime_error("sim: 全局 stall 超阈值,疑似死锁");
  }
  if (!ended) throw std::runtime_error("sim: 程序缺少 END 指令");
}

} // namespace mininpu
