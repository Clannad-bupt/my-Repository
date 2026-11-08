// SramAllocator 实现(README §9.3)。
#include "mininpuc/backend/SramAllocator.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <stdexcept>

namespace mininpu {

namespace {

// 地址有序空闲表:{offset -> size};始终相邻不重叠。
struct FreeList {
  std::map<uint64_t, uint64_t> blocks;

  void insert(uint64_t off, uint64_t size) {
    if (size == 0) return;
    // 与后继合并
    auto next = blocks.lower_bound(off);
    if (next != blocks.end() && next->first == off + size) {
      size += next->second;
      blocks.erase(next);
    }
    // 与前驱合并
    if (!blocks.empty()) {
      auto prev = blocks.lower_bound(off);
      if (prev != blocks.begin()) {
        --prev;
        if (prev->first + prev->second == off) {
          uint64_t noff = prev->first;
          uint64_t nsize = prev->second + size;
          blocks.erase(prev);
          off = noff; size = nsize;
        }
      }
    }
    blocks[off] = size;
  }

  // 在窗口 [winLo, winHi) 内找首个能容纳 size(按 align 对齐后)的块;
  // 找到则切下 [alignedLo, alignedLo+size) 返回 true。
  bool firstFit(uint64_t winLo, uint64_t winHi, uint64_t size, uint64_t align,
                uint64_t& outOff) {
    // 起点:最后一个 first <= winLo 的块(它可能横跨 winLo)
    auto it = blocks.upper_bound(winLo);
    if (it != blocks.begin()) --it;
    for (;; ++it) {
      if (it == blocks.end() || it->first >= winHi) break;
      uint64_t lo = std::max(it->first, winLo);
      uint64_t alignedLo = (lo + align - 1) / align * align;
      uint64_t availEnd = std::min(it->first + it->second, winHi);
      if (alignedLo + size > availEnd) continue;
      // 切开三块:前缀(含对齐空隙)/ 分配 / 后缀
      uint64_t blkLo = it->first, blkSize = it->second;
      blocks.erase(it);
      insert(blkLo, alignedLo - blkLo);
      insert(alignedLo + size, blkLo + blkSize - (alignedLo + size));
      outOff = alignedLo;
      return true;
    }
    return false;
  }
};

} // namespace

std::vector<AllocResult>
SramAllocator::allocate(const std::vector<LiveInterval>& intervals) const {
  const uint64_t sram = spec_.sramBytes;
  const uint64_t align = spec_.dmaAlign;
  const uint32_t nb = std::max(1u, spec_.numBanks);
  const uint64_t bankSize = sram / nb;

  // 校验 + 按 begin 稳定排序的索引
  std::vector<size_t> order(intervals.size());
  for (size_t i = 0; i < intervals.size(); ++i) {
    const auto& iv = intervals[i];
    if (iv.end <= iv.begin)
      throw std::runtime_error("SramAllocator: " + iv.name + " 生命周期为空");
    uint64_t aligned = (iv.sizeBytes + align - 1) / align * align;
    if (aligned > sram)
      throw std::runtime_error("SramAllocator: " + iv.name + " 超过整个 SRAM 容量");
    order[i] = i;
  }
  std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return intervals[a].begin < intervals[b].begin;
  });

  FreeList freeList;
  freeList.insert(0, sram);
  std::vector<AllocResult> results(intervals.size());
  // active: 已分配且仍存活的 (end, offset, size)
  std::vector<std::tuple<int, uint64_t, uint64_t>> active;

  for (size_t idx : order) {
    const auto& iv = intervals[idx];
    uint64_t size = (iv.sizeBytes + align - 1) / align * align;
    // 过期回收
    for (size_t j = 0; j < active.size();) {
      if (std::get<0>(active[j]) <= iv.begin) {
        freeList.insert(std::get<1>(active[j]), std::get<2>(active[j]));
        active.erase(active.begin() + j);
      } else {
        ++j;
      }
    }

    uint64_t off = 0;
    bool ok = false;
    int pb = iv.preferredBank;
    if (pb >= 0 && static_cast<uint32_t>(pb) < nb)
      ok = freeList.firstFit(uint64_t(pb) * bankSize,
                             std::min(uint64_t(pb + 1) * bankSize, sram), size,
                             align, off);
    if (!ok)
      ok = freeList.firstFit(0, sram, size, align, off);
    if (!ok) {
      std::ostringstream os;
      os << "SramAllocator: " << iv.name << "(" << size
         << "B @ 程序点 " << iv.begin << ") 无法分配,SRAM 不足。"
            "禁止静默放 DRAM(§9.3),请调整 tile 或增大 --sram-kb";
      throw std::runtime_error(os.str());
    }
    active.emplace_back(iv.end, off, size);
    results[idx] = {off, static_cast<int>(off / bankSize)};
  }
  return results;
}

} // namespace mininpu
