#include "mininpuc/npy.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace mininpu {

// descr ↔ dtype(二期-INT8 新增 |i1/<i4;三期-FP8 新增 |u1,位重解释:npy 永不
// 表达 f8 语义,写入=位直存,读回由调用方按 desc dtype 解释;npy v1.0,C-order)
static const char* descrOf(DType t) {
  switch (t) {
  case DT_F16: return "<f2";
  case DT_F32: return "<f4";
  case DT_I8: return "|i1";
  case DT_I32: return "<i4";
  case DT_F8E4M3: return "|u1";
  }
  throw std::runtime_error("npy: 未知 dtype");
}
static DType dtypeOfDescr(const std::string& d, const std::string& path) {
  if (d == "<f2") return DT_F16;
  if (d == "<f4") return DT_F32;
  if (d == "|i1") return DT_I8;
  if (d == "|u1") return DT_I8; // 三期(§6.6):torch uint8 存盘;按 1 字节容器
                                // 读回,若 desc 为 F8 由调用方位直通重解释
  if (d == "<i4") return DT_I32;
  throw std::runtime_error("npy: unsupported descr '" + d + "' in " + path);
}

NpyArray readNpy(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw std::runtime_error("npy: cannot open " + path);
  char magic[6];
  if (!is.read(magic, 6) || std::memcmp(magic, "\x93NUMPY", 6) != 0)
    throw std::runtime_error("npy: bad magic in " + path);
  uint8_t major = 0, minor = 0;
  is.read(reinterpret_cast<char*>(&major), 1);
  is.read(reinterpret_cast<char*>(&minor), 1);
  uint32_t hdrLen = 0;
  if (major == 1) {
    uint16_t l = 0;
    is.read(reinterpret_cast<char*>(&l), 2);
    hdrLen = l;
  } else {
    is.read(reinterpret_cast<char*>(&hdrLen), 4);
  }
  std::string hdr(hdrLen, '\0');
  if (!is.read(hdr.data(), hdrLen)) throw std::runtime_error("npy: truncated header");

  NpyArray a;
  auto descrPos = hdr.find("'descr'");
  auto shapePos = hdr.find("'shape'");
  auto fortranPos = hdr.find("'fortran_order'");
  if (descrPos == std::string::npos || shapePos == std::string::npos)
    throw std::runtime_error("npy: header missing descr/shape in " + path);
  if (fortranPos != std::string::npos) {
    auto colon = hdr.find(':', fortranPos);
    auto valPos = hdr.find_first_not_of(' ', colon + 1);
    if (valPos != std::string::npos && hdr.compare(valPos, 4, "True") == 0)
      throw std::runtime_error("npy: fortran_order not supported in " + path);
  }
  auto q1 = hdr.find('\'', descrPos + 7);
  auto q2 = hdr.find('\'', q1 + 1);
  std::string descr = hdr.substr(q1 + 1, q2 - q1 - 1);
  a.dtype = dtypeOfDescr(descr, path);

  auto lp = hdr.find('(', shapePos);
  auto rp = hdr.find(')', lp);
  std::string dims = hdr.substr(lp + 1, rp - lp - 1);
  size_t pos = 0;
  while (pos < dims.size()) {
    while (pos < dims.size() && (dims[pos] == ' ' || dims[pos] == ',')) ++pos;
    if (pos >= dims.size()) break;
    size_t end = dims.find(',', pos);
    std::string tok = dims.substr(pos, end == std::string::npos ? end : end - pos);
    if (!tok.empty()) a.shape.push_back(std::stoll(tok));
    pos = (end == std::string::npos) ? dims.size() : end + 1;
  }
  int64_t bytes = a.numel() * dtypeSize(a.dtype);
  a.data.resize(bytes);
  if (bytes && !is.read(reinterpret_cast<char*>(a.data.data()), bytes))
    throw std::runtime_error("npy: truncated data in " + path);
  return a;
}

void writeNpy(const std::string& path, const NpyArray& a) {
  std::ofstream os(path, std::ios::binary);
  if (!os) throw std::runtime_error("npy: cannot open " + path + " for write");
  std::string dict = "{'descr': '";
  dict += descrOf(a.dtype);
  dict += "', 'fortran_order': False, 'shape': (";
  for (size_t i = 0; i < a.shape.size(); ++i) {
    dict += std::to_string(a.shape[i]);
    if (a.shape.size() == 1) dict += ",";
    if (i + 1 < a.shape.size()) dict += ", ";
  }
  dict += "), }";
  // header 对齐到 64B,末尾 '\n'
  size_t total = 6 + 2 + 2 + dict.size() + 1;
  size_t pad = (64 - (total % 64)) % 64;
  dict.append(pad, ' ');
  dict += '\n';

  os.write("\x93NUMPY", 6);
  uint8_t major = 1, minor = 0;
  os.write(reinterpret_cast<const char*>(&major), 1);
  os.write(reinterpret_cast<const char*>(&minor), 1);
  uint16_t hdrLen = static_cast<uint16_t>(dict.size());
  os.write(reinterpret_cast<const char*>(&hdrLen), 2);
  os.write(dict.data(), dict.size());
  if (!a.data.empty())
    os.write(reinterpret_cast<const char*>(a.data.data()),
             static_cast<std::streamsize>(a.data.size()));
}

} // namespace mininpu
