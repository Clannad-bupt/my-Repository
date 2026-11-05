# IQ 模块列表

本目录包含所有硬件计算模块的 IQ（Instruction Queue）封装。每个 IQ 模块负责将高层控制参数打包为对应硬件模块的微指令格式。

## 模块分类

### 1. 存储与路由
- **ur_iq** - User Register 读写控制（161-bit）
- **rtovr_iq** - 数据路由交叉开关（240-bit，64输入→40输出）
- **shiftup_iq** - 上行数据移位（135-bit）
- **shiftdown_iq** - 下行数据移位（134-bit）

### 2. 整数运算
- **intadd_iq** - 整数加法（4-bit/8-bit/16-bit/32-bit）

### 3. 浮点运算
- **fpmul_iq** - FP32/FP16 乘法（3-bit）
- **fpadd_iq** - FP32/FP16 加法（4-bit，3级流水）
- **fp16mul_iq** - 32路并行 FP16→FP32 乘法（10-bit）

### 4. 浮点加法树
- **fp32addtree5to1_iq** - 5输入 FP32 加法树（1-bit）
- **fp32addtree8to1_iq** - 8输入 FP32 加法树（1-bit）

### 5. 类型转换
- **int8tofp16_iq** - INT8 → FP16（2-bit）
- **inttofp_iq** - INT16/32 → FP16/32（6-bit）
- **fptoint_iq** - FP16/32 → INT16/32（5-bit）
- **fptofp_iq** - FP16 ↔ FP32（5-bit）
- **inttoint_iq** - INT16 ↔ INT32（7-bit）

### 6. 逻辑运算
- **logical_iq** - 16种逻辑操作（6-bit）
  - AND, OR, XOR, NOT, COPY
  - SELECT_GT/EQ/LS（基于状态寄存器）
  - SHIFT, ROTATE, FIND_FIRST_ONE/ZERO

## 使用方法

### 1. 包含头文件
```cpp
#include "iq/iq_api.h"  // 包含所有 IQ 模块
```

### 2. 创建控制结构
```cpp
// 示例：FPMUL
iq::FpMulCru fpmul_cru;
fpmul_cru.para_inst_valid = true;
fpmul_cru.para_src_precision_is_32b = true;  // FP32
fpmul_cru.para_dst_precision_is_32b = true;
```

### 3. 打包微指令
```cpp
// 方式1：打包为 code
uint8_t code = iq::FpMulIq::pack_cru_fpmul(fpmul_cru);

// 方式2：生成 MicroInstruction
MicroInstruction inst = iq::FpMulIq::make_micro_inst(fpmul_cru);
```

### 4. 在 OP 层使用
```cpp
// 在算子中组合多个 IQ 模块
class MyOp {
    void compile() {
        // 1. UR 读
        iq::UrCru ur_cru;
        // ... 设置参数
        uint32_t ur_words[6];
        iq::UrIq::pack_cru_ur_words(ur_cru, ur_words);
        
        // 2. RtoVR 路由
        iq::RtoVrCru rtovr_cru;
        // ... 设置路由
        uint32_t rtovr_words[8];
        iq::RtoVrIq::pack_cru_rtovr_words(rtovr_cru, rtovr_words);
        
        // 3. 计算模块
        iq::FpMulCru fpmul_cru;
        // ... 设置参数
        uint8_t fpmul_code = iq::FpMulIq::pack_cru_fpmul(fpmul_cru);
    }
};
```

## 微指令格式对齐

所有 IQ 模块的微指令格式都严格对齐对应的硬件 RTL 模块
