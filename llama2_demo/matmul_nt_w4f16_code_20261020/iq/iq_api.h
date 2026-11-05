#ifndef SMI_IQ_IQ_API_H
#define SMI_IQ_IQ_API_H

/**
 * @file iq_api.h
 * @brief IQ 层对 OP 层的聚合入口：OP 通常只需 include 本文件即可使用常用 IQ 基类/模块。
 */

// OP 层通常只需要 include 这一个，即可拿到基类和常用模块子类。

#include "smi_iq_base.h"
#include "modules/ur_iq.h"
#include "modules/rtovr_iq.h"
#include "modules/intadd_iq.h"
#include "modules/fpmul_iq.h"
#include "modules/fpadd_iq.h"
#include "modules/fp16mul_iq.h"
#include "modules/logical_iq.h"
#include "modules/int8tofp16_iq.h"
#include "modules/inttofp_iq.h"
#include "modules/fptoint_iq.h"
#include "modules/fptofp_iq.h"
#include "modules/inttoint_iq.h"
#include "modules/fp32addtree5to1_iq.h"
#include "modules/fp32addtree8to1_iq.h"
#include "modules/shiftup_iq.h"
#include "modules/shiftdown_iq.h"
#include "modules/ldb_iq.h"
#include "modules/stb_iq.h"

#endif
