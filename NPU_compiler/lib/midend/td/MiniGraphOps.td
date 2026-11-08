// minigraph dialect — 图级算子(README §5.2)。Tensor 级、无硬件细节。
#ifndef MININPUC_TD_MINIGRAPH
#define MININPUC_TD_MINIGRAPH

include "mlir/IR/OpBase.td"
include "mlir/IR/CommonTypeConstraints.td"
include "mlir/IR/BuiltinAttributes.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

def MiniGraphDialect : Dialect {
  let name = "minigraph";
  let cppNamespace = "::mininpu::graph";
  let summary = "图级算子 dialect(RMSNorm/MatMul/SiLU 等),Tensor 级、无硬件细节";
}

class MiniGraph_Op<string mnemonic, list<Trait> traits = []>
    : Op<MiniGraphDialect, mnemonic, traits>;

// 第一期数值类型:F16 / F32 张量(README §2.3);三期扩 BF16(§6.6)——
// 浮点算子(matmul/rms/silu/mul/add/quant)的操作数全部自动获得 bf16
def AnyF16OrF32Tensor : AnyTypeOf<[F16Tensor, F32Tensor, BF16Tensor], "f16/f32/bf16 tensor">;

def MiniGraph_MatmulOp : MiniGraph_Op<"matmul", [Pure]> {
  let summary = "2D matmul:C = (T_a? A^T : A) @ (T_b? B^T : B) (+bias) (epilogue)";
  let description = [{
    README §5.2.1。`epilogue` 由中端融合 pass 写入:
    缺省 = 无;"silu" = 输出经 SiLU;"bias" = 加 bias;"bias_silu" = 先 bias 后 SiLU。
    bias 为 1-D、长度 N,沿 M 广播(§8.1)。
  }];
  let arguments = (ins
    AnyF16OrF32Tensor:$a,
    AnyF16OrF32Tensor:$b,
    Optional<AnyF16OrF32Tensor>:$bias,
    DefaultValuedAttr<BoolAttr, "false">:$transpose_a,
    DefaultValuedAttr<BoolAttr, "false">:$transpose_b,
    OptionalAttr<StrAttr>:$epilogue
  );
  let results = (outs AnyF16OrF32Tensor:$result);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $a `,` $b (`,` $bias^)? attr-dict
    `:` `(` type($a) `,` type($b) (`,` type($bias)^)? `)` `->` type($result)
  }];
}

def MiniGraph_RmsNormOp : MiniGraph_Op<"rms_norm", [Pure]> {
  let summary = "RMSNorm:y = x / sqrt(mean(x^2) + eps) * gamma,沿最后一维";
  let arguments = (ins
    AnyF16OrF32Tensor:$x,
    AnyF16OrF32Tensor:$gamma,
    DefaultValuedAttr<F64Attr, "1.0e-6">:$eps
  );
  let results = (outs AnyF16OrF32Tensor:$result);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $x `,` $gamma attr-dict `:` `(` type($x) `,` type($gamma) `)` `->` type($result)
  }];
}

def MiniGraph_SiluOp : MiniGraph_Op<"silu", [Pure]> {
  let summary = "y = x * sigmoid(x)(逐元素)";
  let arguments = (ins AnyF16OrF32Tensor:$x);
  let results = (outs AnyF16OrF32Tensor:$result);
  let hasVerifier = 1;
  let assemblyFormat = "$x attr-dict `:` type($x) `->` type($result)";
}

def MiniGraph_MulOp : MiniGraph_Op<"mul", [Pure]> {
  let summary = "逐元素乘(广播;第一期实际只用同形)";
  let arguments = (ins AnyF16OrF32Tensor:$lhs, AnyF16OrF32Tensor:$rhs);
  let results = (outs AnyF16OrF32Tensor:$result);
  let hasVerifier = 1;
  let assemblyFormat =
    "$lhs `,` $rhs attr-dict `:` `(` type($lhs) `,` type($rhs) `)` `->` type($result)";
}

def MiniGraph_AddOp : MiniGraph_Op<"add", [Pure]> {
  let summary = "逐元素加(广播)";
  let arguments = (ins AnyF16OrF32Tensor:$lhs, AnyF16OrF32Tensor:$rhs);
  let results = (outs AnyF16OrF32Tensor:$result);
  let hasVerifier = 1;
  let assemblyFormat =
    "$lhs `,` $rhs attr-dict `:` `(` type($lhs) `,` type($rhs) `)` `->` type($result)";
}

def MiniGraph_TransposeOp : MiniGraph_Op<"transpose", [Pure]> {
  let summary = "按 permutation 转置;中端应尽量消除或折进 matmul.transpose_*";
  let arguments = (ins AnyF16OrF32Tensor:$x, I64ArrayAttr:$permutation);
  let results = (outs AnyF16OrF32Tensor:$result);
  let hasVerifier = 1;
  let assemblyFormat = "$x attr-dict `:` type($x) `->` type($result)";
}

def MiniGraph_ReshapeOp : MiniGraph_Op<"reshape", [Pure]> {
  let summary = "静态 reshape(元素数不变)";
  let arguments = (ins AnyF16OrF32Tensor:$x, I64ArrayAttr:$shape);
  let results = (outs AnyF16OrF32Tensor:$result);
  let hasVerifier = 1;
  let assemblyFormat = "$x attr-dict `:` type($x) `->` type($result)";
}

// 二期-INT8(README §2.3/§6.5):W8A8 对称量化,i32 累加 + 一次性 dequant
def AnyI8Tensor : AnyTypeOf<[TensorOf<[I8]>], "i8 tensor">;
// 三期-FP8(§6.6):8-bit 容器 ∈ {i8, f8E4M3FN}。IR 层 f8 只作容器标签
// (无算术),e4m3 语义(RNE、饱和 ±448、无 Inf、subnormal 到 2⁻⁹,与
// torch.float8_e4m3fn 一致)由模拟器/转换函数定义并单测锁定
def AnyI8OrF8Tensor : AnyTypeOf<[TensorOf<[I8]>, TensorOf<[F8E4M3FN]>],
                                "i8/f8E4M3FN tensor">;

def MiniGraph_QuantOp : MiniGraph_Op<"quant", [Pure]> {
  let summary = [{
    y[m,n] = enc(x[m,n] / sa[m]),容器 ∈ {i8, f8E4M3FN};sa 为 per-token(行)
    或 per-tensor(长度 1,广播)scale
  }];
  let description = [{
    §6.5/§6.6。i8:sat_RNE 对称域 [-127,127],sa[m] = absmax(x[m,:])/127;
    f8E4M3FN:饱和编码到 ±448,per-tensor sa = absmax(x)/448 是标准粒度
    (对数步长只需均匀相对精度标定)。第一期 sa 由 host 侧对图输入计算;
    在线 absmax 单元与逐 gemm 输出直连的量化流水为后续扩展。
  }];
  let arguments = (ins
    AnyF16OrF32Tensor:$x,
    AnyF16OrF32Tensor:$sa   // 1-D,长度=行数(首维)或 1(per-tensor 广播)
  );
  let results = (outs AnyI8OrF8Tensor:$result);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $x `,` $sa attr-dict `:` `(` type($x) `,` type($sa) `)` `->` type($result)
  }];
}

def MiniGraph_QuantMatmulOp : MiniGraph_Op<"quant_matmul", [Pure]> {
  let summary = [{
    W8A8 matmul(a/b 容器 ∈ {i8, f8E4M3FN}),精确/近精确累加后一次性 dequant:
    y[m,n] = (A @ B)[m,n] * sb[n] * sa[m]
  }];
  let description = [{
    §6.5/§6.6。i8:C 块以 DT_I32 驻留 SRAM,跨 Tk tile 走 i32 精确整数累加;
    f8E4M3FN:C 块为 DT_F32——K·448² 溢出 f16,e4m3×e4m3 乘积(≤8 位有效
    尾数)在 f32 精确可表示,累加误差仅 f32 RNE(H100 WGMMA 口径)。两格式
    对比 f16 C 块路径每 Tk 边界的部分和量化(§2.3),dequant 都只在 K 全部
    归约完后做一次。sa 长度 ∈ {M,1}、sb ∈ {N,1}(长度 1 = per-tensor,
    FP8 的标准粒度)。不支持 transpose/bias/epilogue(f16 matmul 的
    epilogue 仍可在 dequant 后接独立 vec op 表达同样的图)。
  }];
  let arguments = (ins
    AnyI8OrF8Tensor:$a,           // [M,K]
    AnyI8OrF8Tensor:$b,           // [K,N]
    AnyF16OrF32Tensor:$sa,        // 1-D [M] 或 [1],激活 scale
    AnyF16OrF32Tensor:$sb         // 1-D [N] 或 [1],权重 scale
  );
  let results = (outs AnyF16OrF32Tensor:$result);
  let hasVerifier = 1;
  let assemblyFormat = [{
    $a `,` $b `,` $sa `,` $sb attr-dict
    `:` `(` type($a) `,` type($b) `,` type($sa) `,` type($sb) `)` `->` type($result)
  }];
}

// 第二期预留(README §5.2.5)
def MiniGraph_RopeOp : MiniGraph_Op<"rope", [Pure]> {
  let summary = "旋转位置编码(第二期,接口预留)";
  let arguments = (ins
    AnyF16OrF32Tensor:$x,
    AnyF16OrF32Tensor:$cos,
    AnyF16OrF32Tensor:$sin,
    I64Attr:$rotary_dim
  );
  let results = (outs AnyF16OrF32Tensor:$result);
  let assemblyFormat =
    "$x `,` $cos `,` $sin attr-dict `:` `(` type($x) `,` type($cos) `,` type($sin) `)` `->` type($result)";
}

#endif // MININPUC_TD_MINIGRAPH
