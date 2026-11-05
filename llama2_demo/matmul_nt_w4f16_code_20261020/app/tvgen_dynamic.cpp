#include "tvgen.h"

/**
 * @file tvgen_dynamic.cpp
 * @brief TVGen 动态编译部分：使用 OpRegistry 动态加载算子
 */

#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdlib>

#include "fp16_utils.h"
#include "../op/common/op_registry.h"

using namespace aiacc;
using namespace aiacc::app;
using namespace aiacc::op;

// ==========================================
// Compiler: 动态加载算子生成微指令
// ==========================================

bool Tvgen::compile(const AppOperatorRequest& para_req,
                   std::vector<uint8_t>& para_inst_bin,
                   std::vector<op::IqInstructionFile>& para_iq_files) {
    
    // 从注册器获取算子
    OpBase* op = OpRegistry::get_instance().get_op(para_req.para_operator);
    if (!op) {
        return false;
    }
    
    // 调用算子的编译方法
    OpInstructionsBase* insts_ptr = op->compile(para_req);
    if (!insts_ptr) {
        return false;
    }
    
    // 序列化指令
    para_iq_files = insts_ptr->get_iq_files();
    para_inst_bin = insts_ptr->serialize();
    
    // 释放指令对象
    delete insts_ptr;
    
    return true;
}