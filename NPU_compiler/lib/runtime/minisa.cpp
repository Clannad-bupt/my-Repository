#include "mininpuc/minisa.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace mininpu {

static constexpr uint16_t kMinisaVersion = 1;
static constexpr uint16_t kHdrSize = 36;

const Descriptor* Executable::findDesc(uint32_t id) const {
  for (const auto& d : descs)
    if (d.id == id) return &d;
  return nullptr;
}

template <typename T>
static void put(std::ostream& os, T v) {
  os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

template <typename T>
static T get(std::istream& is, const char* what) {
  T v{};
  if (!is.read(reinterpret_cast<char*>(&v), sizeof(T)))
    throw std::runtime_error(std::string("minisa: truncated while reading ") + what);
  return v;
}

void writeExecutable(const std::string& path, const Executable& exe) {
  if (exe.descs.size() > 256)
    throw std::runtime_error("minisa: descriptor count " +
                             std::to_string(exe.descs.size()) + " > 256 (id 是 8-bit)");
  std::ofstream os(path, std::ios::binary);
  if (!os) throw std::runtime_error("minisa: cannot open " + path + " for write");

  os.write("MNPU", 4);
  put<uint16_t>(os, kMinisaVersion);
  put<uint16_t>(os, kHdrSize);
  put<uint32_t>(os, static_cast<uint32_t>(exe.descs.size()));
  put<uint32_t>(os, static_cast<uint32_t>(exe.code.size() * 8)); // CodeSize 字节计
  put<uint32_t>(os, static_cast<uint32_t>(exe.constants.size()));
  char reserved[16] = {0};
  os.write(reserved, 16);

  for (const auto& d : exe.descs) {
    put<uint32_t>(os, d.id);
    put<uint8_t>(os, d.memspace);
    put<uint8_t>(os, d.dtype);
    put<uint8_t>(os, d.rank);
    put<uint8_t>(os, d.align);
    for (int i = 0; i < 4; ++i) put<uint32_t>(os, d.shape[i]);
    for (int i = 0; i < 4; ++i) put<int32_t>(os, d.stride[i]);
    put<uint64_t>(os, d.base);
    put<uint64_t>(os, d.bytes);
  }
  for (uint64_t w : exe.code) put<uint64_t>(os, w);
  if (!exe.constants.empty())
    os.write(reinterpret_cast<const char*>(exe.constants.data()),
             static_cast<std::streamsize>(exe.constants.size()));
  if (!os) throw std::runtime_error("minisa: write failed on " + path);
}

Executable readExecutable(const std::string& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw std::runtime_error("minisa: cannot open " + path);

  char magic[4];
  if (!is.read(magic, 4) || std::memcmp(magic, "MNPU", 4) != 0)
    throw std::runtime_error("minisa: bad magic in " + path + " (not a .minisa file)");
  uint16_t version = get<uint16_t>(is, "version");
  if (version != kMinisaVersion)
    throw std::runtime_error("minisa: version mismatch: file=" + std::to_string(version) +
                             " runtime=" + std::to_string(kMinisaVersion));
  get<uint16_t>(is, "hdrsize");
  uint32_t descCount = get<uint32_t>(is, "desccount");
  uint32_t codeSize = get<uint32_t>(is, "codesize");
  uint32_t constSize = get<uint32_t>(is, "constsize");
  char reserved[16];
  if (!is.read(reserved, 16)) throw std::runtime_error("minisa: truncated header");

  Executable exe;
  exe.descs.resize(descCount);
  for (auto& d : exe.descs) {
    d.id = get<uint32_t>(is, "desc.id");
    d.memspace = get<uint8_t>(is, "desc.memspace");
    d.dtype = get<uint8_t>(is, "desc.dtype");
    d.rank = get<uint8_t>(is, "desc.rank");
    d.align = get<uint8_t>(is, "desc.align");
    for (int i = 0; i < 4; ++i) d.shape[i] = get<uint32_t>(is, "desc.shape");
    for (int i = 0; i < 4; ++i) d.stride[i] = get<int32_t>(is, "desc.stride");
    d.base = get<uint64_t>(is, "desc.base");
    d.bytes = get<uint64_t>(is, "desc.bytes");
  }
  if (codeSize % 8 != 0)
    throw std::runtime_error("minisa: CodeSize not multiple of 8");
  exe.code.resize(codeSize / 8);
  for (auto& w : exe.code) w = get<uint64_t>(is, "code");
  exe.constants.resize(constSize);
  if (constSize && !is.read(reinterpret_cast<char*>(exe.constants.data()), constSize))
    throw std::runtime_error("minisa: truncated const section");
  return exe;
}

} // namespace mininpu
