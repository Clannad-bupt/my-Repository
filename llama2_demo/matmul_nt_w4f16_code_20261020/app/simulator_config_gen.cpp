#include "simulator_config_gen.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <set>

namespace aiacc {
namespace app {

std::string get_simulator_iq_name(uint32_t iq_id, const std::string& iq_name) {
    (void)iq_id;
    if (iq_name == "ur_rdata_0" || iq_name == "ur-r0") return "ur-r0";
    if (iq_name == "ur_rdata_1" || iq_name == "ur-r1") return "ur-r1";
    if (iq_name == "ur_rdata_2" || iq_name == "ur-r2") return "ur-r2";
    if (iq_name == "ur_rdata_3" || iq_name == "ur-r3") return "ur-r3";
    if (iq_name == "ur_wdata_0" || iq_name == "ur-w0") return "ur-w0";
    if (iq_name == "ur_wdata_1" || iq_name == "ur-w1") return "ur-w1";
    if (iq_name == "ur_wdata_2" || iq_name == "ur-w2") return "ur-w2";
    if (iq_name == "ur_wdata_3" || iq_name == "ur-w3") return "ur-w3";
    if (iq_name == "fpadd") return "fpadd";
    if (iq_name == "fpmul") return "fpmul";
    if (iq_name == "intadd") return "intadd";
    if (iq_name == "fp16mul") return "fp16mul";
    if (iq_name == "fptofp") return "fptofp";
    if (iq_name == "inttofp") return "inttofp";
    if (iq_name == "fptoint") return "fptoint";
    if (iq_name == "logic_neg" || iq_name == "logic") return "logic";
    if (iq_name == "shiftup") return "shiftup";
    if (iq_name == "shiftdn") return "shiftdn";
    return "";
}

void write_simulator_config(
    const std::string& output_dir,
    const std::map<std::string, std::string>& iq_init,
    const std::vector<int>& data_bank_ids,
    int output_bank_id,
    int max_cycles,
    const std::string& dtype,
    int max_addr,
    const std::map<int, std::string>& bank_names,
    int output_start_addr,
    const std::string& op_name) {

    std::string path = output_dir + "/simulator_config.json";
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "{\n";
    out << "  \"test_name\": \"SMI_IQ_generated\",\n";
    out << "  \"operator\": \"" << op_name << "\",\n";
    out << "  \"dtype\": \"" << dtype << "\",\n";
    out << "  \"iq_init\": {\n";
    bool first = true;
    for (const auto& kv : iq_init) {
        if (!first) out << ",\n";
        out << "    \"" << kv.first << "\": \"" << kv.second << "\"";
        first = false;
    }
    out << "\n  },\n";
    out << "  \"ur_init\": {\n";
    
    // Combine all banks (input, other, output, etc.) to ensure they are all initialized
    std::set<int> all_banks;
    for (int b : data_bank_ids) all_banks.insert(b);
    all_banks.insert(output_bank_id);
    for (const auto& kv : bank_names) all_banks.insert(kv.first);
    
    first = true;
    for (int b : all_banks) {
        if (!first) out << ",\n";
        out << "    \"" << b << "\": \"ur_0" << b << ".hex\"";
        first = false;
    }
    out << "\n  },\n";
    // [BUG FIX] 此处移除冗余引入的重复 ur_init JSON 循环生成代码。
    // 该冗余代码会导致 JSON 多余出无键值的尾部结构以及括号不匹配，从而引起 smc_simulator 发生 JSONDecodeError 解析崩溃。
    out << "  \"settings\": {\n";
    out << "    \"max_cycles\": " << max_cycles << ",\n";
    out << "    \"early_termination\": true,\n";
    out << "    \"drain_cycles\": 10,\n";
    out << "    \"output_banks\": [" << output_bank_id << "],\n";
    out << "    \"output_start_addr\": " << output_start_addr << ",\n";
    out << "    \"max_addr\": " << max_addr << "\n";
    out << "  },\n";
    out << "  \"bank_names\": {\n";
    {
        bool first_name = true;
        for (const auto& [bid, name] : bank_names) {
            if (!first_name) out << ",\n";
            out << "    \"" << bid << "\": \"" << name << "\"";
            first_name = false;
        }
    }
    out << "\n  }\n";
    out << "}\n";
}

}
}
