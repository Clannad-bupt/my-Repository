#include "mininpuc/HardwareSpec.h"
#include "mininpuc/Json.h"

namespace mininpu {

HardwareSpec HardwareSpec::loadFromJsonFile(const std::string& path) {
  HardwareSpec s;
  JsonValue j = readJsonFile(path);
  auto getU = [&](const char* k, auto& dst) {
    if (const JsonValue* v = j.find(k))
      dst = static_cast<std::decay_t<decltype(dst)>>(v->asNum());
  };
  auto getD = [&](const char* k, double& dst) {
    if (const JsonValue* v = j.find(k)) dst = v->asNum();
  };
  getU("sram_bytes", s.sramBytes);
  getU("num_banks", s.numBanks);
  getU("dma_align", s.dmaAlign);
  getD("dram_bw_GBps", s.dramBwGBps);
  getU("freq_hz", s.freqHz);
  getU("macs_per_cycle", s.macsPerCycle);
  getU("macs_per_cycle_int8", s.macsPerCycleInt8);
  getU("macs_per_cycle_fp8", s.macsPerCycleFp8);
  getU("dma_latency_cycles", s.dmaLatencyCycles);
  getU("vec_rms_alpha", s.vecRmsAlpha);
  getU("vec_bw_bytes_per_cycle", s.vecBwBytesPerCycle);
  getD("tile_lambda", s.tileLambda);
  getD("tile_mu", s.tileMu);
  if (s.numBanks == 0 || s.macsPerCycle == 0 || s.freqHz == 0 ||
      s.macsPerCycleInt8 == 0 || s.macsPerCycleFp8 == 0)
    throw std::runtime_error(
        "HardwareSpec: num_banks/macs_per_cycle/macs_per_cycle_int8/"
        "macs_per_cycle_fp8/freq_hz 不能为 0");
  return s;
}

} // namespace mininpu
