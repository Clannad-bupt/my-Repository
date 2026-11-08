// mininpu dialect — 硬件级算子(README §5.3)。Buffer/SRAM 级、带 token 依赖。
#ifndef MININPUC_TD_MININPU
#define MININPUC_TD_MININPU

include "mlir/IR/OpBase.td"
include "mlir/IR/CommonTypeConstraints.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def MiniNPUDialect : Dialect {
  let name = "mininpu";
  let cppNamespace = "::mininpu::npu";
  let summary = "硬件级 dialect:sram 分配、DMA、GEMM tile、vec 操作,带显式 token";
  let useDefaultTypePrinterParser = 1;
}

class MiniNPU_Op<string mnemonic, list<Trait> traits = []>
    : Op<MiniNPUDialect, mnemonic, traits>;

// 异步 token(README §5.5):async op 产出,sync 等待
def MiniNPU_TokenType : TypeDef<MiniNPUDialect, "Token"> {
  let summary = "async token:表示 DMA/计算引擎的异步完成事件";
  let mnemonic = "token";
}

// Memory space(README §5.3):0 = dram,1 = sram。用 memref 的 memorySpace attr 表达。

def MiniNPU_SramAllocOp : MiniNPU_Op<"sram_alloc", [NoMemoryEffect]> {
  let summary = "分配片上 SRAM buffer,类型即形状与 memspace(memref<..., 1>)";
  let arguments = (ins I64Attr:$base_offset); // 静态规划好的 SRAM 偏移(字节)
  let results = (outs AnyMemRef:$buffer);
  let assemblyFormat = "attr-dict `:` type($buffer)";
}

def MiniNPU_SramDeallocOp : MiniNPU_Op<"sram_dealloc", [NoMemoryEffect]> {
  let summary = "释放 SRAM buffer(供生命周期分析;第一期为标记用途)";
  let arguments = (ins AnyMemRef:$buffer);
  let assemblyFormat = "$buffer attr-dict `:` type($buffer)";
}

def MiniNPU_DramAllocOp : MiniNPU_Op<"dram_alloc", [NoMemoryEffect]> {
  let summary = "声明一个 DRAM buffer(中间结果/输出);base 由 runtime 分配回填(§6.4 meta.json)";
  let arguments = (ins
    I64Attr:$align,             // 分配对齐(字节)
    OptionalAttr<StrAttr>:$name // meta.json 中的可读名(输出/调试用)
  );
  let results = (outs AnyMemRef:$buffer); // memref<..., 0>(dram)
  let assemblyFormat = "attr-dict `:` type($buffer)";
}

def MiniNPU_ViewOp : MiniNPU_Op<"view", [NoMemoryEffect]> {
  let summary = [{
    纯元数据视图:同一块内存的 shape/stride/offset 重解释,不搬数据。
    用途:SRAM 残块 tile(同槽位小形状)、DRAM transpose 视图(strided layout)、
    reshape 别名。base = 父 base + layout offset*es;DRAM 视图的 base 由 runtime
    经 meta.json views 表回填(父 base + offset)。
  }];
  let arguments = (ins AnyMemRef:$src);
  let results = (outs AnyMemRef:$out);
  let assemblyFormat = "$src attr-dict `:` type($src) `->` type($out)";
}

def MiniNPU_DmaLoadOp : MiniNPU_Op<"dma_load", [MemoryEffects<[MemRead, MemWrite]>]> {
  let summary = "DRAM→SRAM 拷贝;几何取 SRAM 侧形状,imm 为 DRAM 侧字节偏移";
  let arguments = (ins
    AnyMemRef:$src,       // dram
    AnyMemRef:$dst,       // sram
    I64Attr:$byte_offset  // 加在 src base 上的字节偏移(§9.6 slice 寻址)
  );
  let results = (outs MiniNPU_TokenType:$token);
  let assemblyFormat =
    "$src `,` $dst attr-dict `:` `(` type($src) `,` type($dst) `)`";
}

def MiniNPU_DmaStoreOp : MiniNPU_Op<"dma_store", [MemoryEffects<[MemRead, MemWrite]>]> {
  let summary = "SRAM→DRAM 拷贝;几何取 SRAM 侧形状,imm 为 DRAM 侧字节偏移";
  let arguments = (ins
    AnyMemRef:$src,       // sram
    AnyMemRef:$dst,       // dram
    MiniNPU_TokenType:$wait,
    I64Attr:$byte_offset
  );
  let results = (outs MiniNPU_TokenType:$token);
  let assemblyFormat =
    "$src `,` $dst `,` `wait` $wait attr-dict `:` `(` type($src) `,` type($dst) `)`";
}

def MiniNPU_GemmTileOp : MiniNPU_Op<"gemm_tile", [MemoryEffects<[MemRead, MemWrite]>]> {
  let summary = "SRAM 内 GEMM tile:C[+=] A @ B(累加模式由 acc_mode 指定)";
  let arguments = (ins
    AnyMemRef:$a,         // sram Tm x Tk
    AnyMemRef:$b,         // sram Tk x Tn
    AnyMemRef:$c,         // sram Tm x Tn
    MiniNPU_TokenType:$wait,
    I64Attr:$acc_mode     // 0 = overwrite,1 = accumulate(README §6.3)
  );
  let results = (outs MiniNPU_TokenType:$token);
  let assemblyFormat = [{
    $a `,` $b `,` $c `,` `wait` $wait attr-dict
    `:` `(` type($a) `,` type($b) `,` type($c) `)`
  }];
}

def MiniNPU_VecRmsOp : MiniNPU_Op<"vec_rms", [MemoryEffects<[MemRead, MemWrite]>]> {
  let summary = "SRAM 上 RMSNorm:y = x/sqrt(mean(x^2)+eps)*gamma,沿最后一维";
  let arguments = (ins
    AnyMemRef:$x,
    AnyMemRef:$gamma,
    AnyMemRef:$y,
    MiniNPU_TokenType:$wait,
    F64Attr:$eps
  );
  let results = (outs MiniNPU_TokenType:$token);
  let assemblyFormat = [{
    $x `,` $gamma `,` $y `,` `wait` $wait attr-dict
    `:` `(` type($x) `,` type($gamma) `,` type($y) `)`
  }];
}

class MiniNPU_VecEltwise<string mnemonic> :
    MiniNPU_Op<mnemonic, [MemoryEffects<[MemRead, MemWrite]>]> {
  let arguments = (ins
    AnyMemRef:$a,
    AnyMemRef:$b,         // silu 不用(占位传 a)
    AnyMemRef:$y,
    MiniNPU_TokenType:$wait
  );
  let results = (outs MiniNPU_TokenType:$token);
  let assemblyFormat =
    "$a `,` $b `,` $y `,` `wait` $wait attr-dict `:` `(` type($a) `,` type($b) `,` type($y) `)`";
}

def MiniNPU_VecSiluOp : MiniNPU_VecEltwise<"vec_silu"> {
  let summary = "y = silu(a)(b 占位)";
}
def MiniNPU_VecMulOp : MiniNPU_VecEltwise<"vec_mul"> {
  let summary = "y = a * b(逐元素)";
}
def MiniNPU_VecAddOp : MiniNPU_VecEltwise<"vec_add"> {
  let summary = "y = a + b(逐元素)";
}

// 二期-INT8(§6.5)/ 三期-FP8(§6.6):量化/反量化入 vec 通道
def MiniNPU_VecQuantOp : MiniNPU_Op<"vec_quant", [MemoryEffects<[MemRead, MemWrite]>]> {
  let summary = [{
    y[m,n] = enc(x[m,n] / sa[m]):x 为 rank-2 f16/f32/bf16,sa 为 1-D 行
    scale(长度=行数 per-token,或 1 per-tensor 广播),y 为同形容器——
    i8 走 sat_RNE 对称域 ±127(§6.5);f8E4M3 走 e4m3 饱和编码 ±448(§6.6)。
  }];
  let arguments = (ins
    AnyMemRef:$x,          // sram,rank-2 f16/f32
    AnyMemRef:$sa,         // sram,1-D len=行数
    AnyMemRef:$y,          // sram,rank-2 i8
    MiniNPU_TokenType:$wait
  );
  let results = (outs MiniNPU_TokenType:$token);
  let assemblyFormat =
    "$x `,` $sa `,` $y `,` `wait` $wait attr-dict `:` `(` type($x) `,` type($sa) `,` type($y) `)`";
}

def MiniNPU_VecDequantOp : MiniNPU_Op<"vec_dequant", [MemoryEffects<[MemRead, MemWrite]>]> {
  let summary = [{
    y[m,n] = x[m,n] * sb[n] × sa[m]:x 为 rank-2 i32(GEMM_I8 的 C 块)或
    f32(GEMM_F8 的 C 块,§6.6),sb/sa 长度 ∈ {末维,1}/{行数,1}
    (per-channel/tensor、per-token/tensor)。sa 经指令 imm_hi 低 8 bit 的
    第 4 描述符携带(§6.5);y 为 f16/bf16。
  }];
  let arguments = (ins
    AnyMemRef:$x,          // sram,rank-2 i32(GEMM_I8 的 C 块)
    AnyMemRef:$sb,         // sram,1-D len=末维
    AnyMemRef:$sa,         // sram,1-D len=行数
    AnyMemRef:$y,          // sram,rank-2 f16
    MiniNPU_TokenType:$wait
  );
  let results = (outs MiniNPU_TokenType:$token);
  let assemblyFormat = [{
    $x `,` $sb `,` $sa `,` $y `,` `wait` $wait attr-dict
    `:` `(` type($x) `,` type($sb) `,` type($sa) `,` type($y) `)`
  }];
}

def MiniNPU_SyncOp : MiniNPU_Op<"sync", [NoMemoryEffect]> {
  let summary = "等待一组 token 完成(lowering 为对应通道的 BAR)";
  let arguments = (ins Variadic<MiniNPU_TokenType>:$waits);
  let assemblyFormat = "$waits attr-dict";
}

#endif // MININPUC_TD_MININPU
