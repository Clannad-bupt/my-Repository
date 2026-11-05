#ifndef SIMULATOR_CONFIG_GEN_H
#define SIMULATOR_CONFIG_GEN_H

/**
 * @file simulator_config_gen.h
 * @brief 生成 SMC Simulator 使用的 sim_config.json
 */

#include <string>
#include <map>
#include <vector>

namespace aiacc {
namespace app {

/** SMI_IQ (iq_id, iq_name) -> SMC Simulator 组件名（iq_init 键） */
std::string get_simulator_iq_name(uint32_t iq_id, const std::string& iq_name);

/**
 * 写出 sim_config.json 到 output_dir/sim_config.json
 * @param data_bank_ids 有 ur_XX.hex 数据的 bank ID 列表
 * @param output_bank_id 结果所在 bank
 * @param bank_names     bank -> 人类可读名称
 */
void write_simulator_config(
    const std::string& output_dir,
    const std::map<std::string, std::string>& iq_init,
    const std::vector<int>& data_bank_ids,
    int output_bank_id,
    int max_cycles = 500,
    const std::string& dtype = "fp32",
    int max_addr = 256,
    const std::map<int, std::string>& bank_names = {},
    int output_start_addr = 0,
    const std::string& op_name = "add");

}
}

#endif
