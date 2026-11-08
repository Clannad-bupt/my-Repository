#pragma once

#include "mininpuc/minisa.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mininpu {

// 最小 .npy v1.0 读写（C-order，<f2 / <f4 / |i1 / <i4）
struct NpyArray {
  std::vector<int64_t> shape;
  DType dtype = DT_F32;
  std::vector<uint8_t> data; // 原始字节,小端

  int64_t numel() const {
    int64_t n = 1;
    for (int64_t d : shape) n *= d;
    return n;
  }
};

NpyArray readNpy(const std::string& path);
void writeNpy(const std::string& path, const NpyArray& a);

} // namespace mininpu
