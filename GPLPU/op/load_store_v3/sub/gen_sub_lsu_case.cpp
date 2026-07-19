#include <iostream>
#include <exception>
#include <string>

#include "sub_lsu_op.h"

int main(int argc, char* argv[]) {
    std::string case_root = "/home/jabari/Jianyuan_aiacc_v4.19/test_atomic_ops/verification_v2/sub_lsu_v3_test";
    std::string hbm_dir = "/home/jabari/Jianyuan_aiacc_v4.19/data_engine/sram";
    uint32_t seed = 12345;

    if (argc > 1) case_root = argv[1];
    if (argc > 2) hbm_dir = argv[2];
    if (argc > 3) seed = static_cast<uint32_t>(std::stoul(argv[3]));

    try {
        aiacc::lsu::SubLsuOp op;
        op.set_compute_desc(aiacc::lsu::SubLsuOp::default_compute_desc());

        aiacc::lsu::LsuCaseConfig cfg;
        cfg.case_name = "sub_lsu_v3_test";
        cfg.case_root = case_root;
        cfg.hbm_init_dir = hbm_dir;
        cfg.seed_base = seed;
        op.set_case_config(cfg);

        if (!op.assemble_full_case()) {
            std::cerr << "ERROR: assemble_full_case failed" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "SUCCESS: case generated at " << case_root << std::endl;
    return 0;
}
