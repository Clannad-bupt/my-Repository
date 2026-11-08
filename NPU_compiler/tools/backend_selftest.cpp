// backend_selftest — 步骤 4/5 验收(README §13):
//   步骤 4:sram=64KiB 与 256KiB 各打印一套 tile,断言 64KiB 的 tile 更小;
//          planRowTile 在单行都放不下时抛错。
//   步骤 5:三个同时存活的 buffer 分配结果互不重叠;容量不足时报错而非静默。
// 退出码 0 = 通过;失败打印 FAIL 并以 1 退出。
#include "mininpuc/HardwareSpec.h"
#include "mininpuc/backend/SramAllocator.h"
#include "mininpuc/backend/TilePlanner.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mininpu;

static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

static bool rangesOverlap(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1) {
  return a0 < b1 && b0 < a1;
}

// ---- 步骤 4:TilePlanner ----
static void testTilePlanner() {
  const int64_t M = 256, N = 256, K = 256; // README §13 步骤 4 用例
  const uint32_t es = 2;

  HardwareSpec small; // 64KiB
  small.sramBytes = 64 * 1024;
  HardwareSpec large; // 256KiB(默认)
  large.sramBytes = 256 * 1024;

  TilePlan ps = planGemmTile(M, N, K, es, small);
  TilePlan pl = planGemmTile(M, N, K, es, large);
  std::printf("  64KiB : %s\n", ps.reason.c_str());
  std::printf("  256KiB: %s\n", pl.reason.c_str());

  CHECK(ps.sramBytesUsed <= small.sramBytes, "64KiB 计划超容量");
  CHECK(pl.sramBytesUsed <= large.sramBytes, "256KiB 计划超容量");
  // §13 验收:小 SRAM 的 tile 更小(占用字节更少)
  CHECK(ps.sramBytesUsed < pl.sramBytesUsed,
        "64KiB 的 tile 应比 256KiB 的小(§13 步骤 4)");
  // 完整性:tile 覆盖整个问题
  CHECK(int64_t(ps.tilesM) * ps.tm >= M && int64_t(ps.tilesN) * ps.tn >= N &&
            int64_t(ps.tilesK) * ps.tk >= K,
        "tile 必须覆盖 M/N/K");
  // 16 倍数约束
  CHECK(ps.tm % 16 == 0 && ps.tn % 16 == 0 && ps.tk % 16 == 0,
        "tile 必须是 16 倍数(§9.2)");
  // traffic 公式校验(§9.2)
  uint64_t expectTraffic = uint64_t(ps.tilesN) * M * K * es +
                           uint64_t(ps.tilesM) * K * N * es +
                           uint64_t(M) * N * es;
  CHECK(ps.trafficBytes == expectTraffic, "traffic 公式与 §9.2 不一致");
  // 确定性:同输入必须同输出
  TilePlan ps2 = planGemmTile(M, N, K, es, small);
  CHECK(ps2.tm == ps.tm && ps2.tn == ps.tn && ps2.tk == ps.tk,
        "TilePlanner 必须是确定性纯函数");

  // 残块:不能被 16 整除的 K
  TilePlan pr = planGemmTile(100, 200, 300, es, large);
  CHECK(pr.remainderTiles >= 1, "100/200/300 应产生残块标记");
  CHECK(int64_t(pr.tilesM) * pr.tm >= 100 && int64_t(pr.tilesN) * pr.tn >= 200 &&
            int64_t(pr.tilesK) * pr.tk >= 300,
        "残块 tile 覆盖");

  // 容量极小:任何 16 倍数 tile 都放不下 → 必须抛错(§9.3)
  HardwareSpec tiny;
  tiny.sramBytes = 1024; // 2*(16*16+16*16)*2 + 16*16*2 = 2048B > 1024B
  bool threw = false;
  try {
    (void)planGemmTile(M, N, K, es, tiny);
  } catch (const std::runtime_error& e) {
    threw = std::strstr(e.what(), "SRAM") != nullptr;
  }
  CHECK(threw, "极小 SRAM 必须抛错而非静默降级(§9.3)");

  // planRowTile:RMS/vec 行块
  uint32_t tr = planRowTile(/*h=*/512, es, /*buffers=*/2, large);
  CHECK(tr % 16 == 0, "Tr 必须是 16 倍数");
  CHECK(uint64_t(tr) * 512 * es * 2 <= large.sramBytes, "Tr 超容量");
  HardwareSpec cramped;
  cramped.sramBytes = 512; // 一行 512*2*2=2048B 都放不下
  threw = false;
  try {
    (void)planRowTile(512, es, 2, cramped);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw, "一行都放不下必须报错(§13 步骤 4)");
}

// ---- 步骤 5:SramAllocator ----
static void testSramAllocator() {
  HardwareSpec spec; // 256KiB, 4 banks, align 32

  // 三个同时存活的 buffer(A→bank0 B→bank1 C→bank2)互不重叠(§13 步骤 5)
  {
    std::vector<LiveInterval> ivs = {
        {"A", 16 * 1024, 0, 10, 0},
        {"B", 16 * 1024, 1, 10, 1},
        {"C", 16 * 1024, 2, 10, 2},
    };
    auto res = SramAllocator(spec).allocate(ivs);
    CHECK(res[0].bank == 0 && res[1].bank == 1 && res[2].bank == 2,
          "bank 着色:A→bank0 B→bank1 C→bank2(§9.3)");
    for (int i = 0; i < 3; ++i) {
      CHECK(res[i].base % spec.dmaAlign == 0, "分配必须对齐 dmaAlign");
      for (int j = i + 1; j < 3; ++j)
        CHECK(!rangesOverlap(res[i].base, res[i].base + 16 * 1024, res[j].base,
                             res[j].base + 16 * 1024),
              "同时存活的 buffer 不得重叠(§13 步骤 5)");
    }
  }

  // 生命周期不重叠 → 地址复用
  {
    std::vector<LiveInterval> ivs = {
        {"X", 4096, 0, 5, -1},
        {"Y", 4096, 5, 9, -1}, // X 在 5 结束,Y 从 5 开始:可复用
    };
    auto res = SramAllocator(spec).allocate(ivs);
    CHECK(res[0].base == res[1].base, "不重叠生命周期应复用同一地址");
  }

  // 容量不足 → 抛错(§13 步骤 5)
  {
    HardwareSpec tight;
    tight.sramBytes = 48 * 1024; // 三个 16KiB + 对齐后 > 48KiB
    std::vector<LiveInterval> ivs = {
        {"A", 16 * 1024, 0, 10, -1},
        {"B", 16 * 1024, 1, 10, -1},
        {"C", 16 * 1024, 2, 10, -1},
        {"D", 16 * 1024, 3, 10, -1},
    };
    bool threw = false;
    try {
      (void)SramAllocator(tight).allocate(ivs);
    } catch (const std::runtime_error& e) {
      threw = std::strstr(e.what(), "SRAM") != nullptr;
    }
    CHECK(threw, "容量不足必须报错而非静默(§9.3/§13 步骤 5)");
  }

  // 无偏好 buffer 全量同时存活,验证线性扫描 + 首次适配无重叠(压力小例)
  {
    std::vector<LiveInterval> ivs;
    for (int i = 0; i < 8; ++i)
      ivs.push_back({"t" + std::to_string(i), 8 * 1024, 0, 10, -1});
    auto res = SramAllocator(spec).allocate(ivs);
    for (int i = 0; i < 8; ++i)
      for (int j = i + 1; j < 8; ++j)
        CHECK(!rangesOverlap(res[i].base, res[i].base + 8 * 1024, res[j].base,
                             res[j].base + 8 * 1024),
              "压力用例重叠");
  }
}

int main() {
  std::printf("[步骤 4] TilePlanner:\n");
  testTilePlanner();
  std::printf("[步骤 5] SramAllocator:\n");
  testSramAllocator();
  if (g_failures == 0) {
    std::printf("backend_selftest: ALL PASS\n");
    return 0;
  }
  std::printf("backend_selftest: %d FAILURES\n", g_failures);
  return 1;
}
