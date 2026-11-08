// 三期-FP8/BF16(README §6.6):f8 容器 quant/quant_matmul 与 bf16 matmul 的
// mininpu 模板展开 FileCheck。关注:f8E4M3FN 槽位、gemm_tile(f8,f8→f32)、
// vec_dequant 的 f32 源、len-1(per-tensor)scale 的 stride-0 广播 DMA 与
// (1,) vec 视图、bf16 三元一致浮点路径。
// RUN 方式见 test/mlir/test_passes.py(CASES 表)。

func.func @qmm_f8(%a: tensor<64x512xf8E4M3FN>, %b: tensor<512x64xf8E4M3FN>,
                  %sa: tensor<1xf16>, %sb: tensor<1xf16>) -> tensor<64x64xf16> {
  %y = minigraph.quant_matmul %a, %b, %sa, %sb
       : (tensor<64x512xf8E4M3FN>, tensor<512x64xf8E4M3FN>, tensor<1xf16>, tensor<1xf16>)
         -> tensor<64x64xf16>
  return %y : tensor<64x64xf16>
}

// CHECK-LABEL: func.func @qmm_f8(
// per-tensor scale:DRAM 侧 stride-0 广播视图(1 个元素复制 16 份,32B inner),
// 在 DramRef 构造期创建,先于输出/槽位分配
// CHECK: mininpu.view {{.*}} : memref<1xf16> -> memref<16xf16, strided<[0]>>
// CHECK: mininpu.view {{.*}} : memref<1xf16> -> memref<16xf16, strided<[0]>>
// 输出 buffer:f16 DRAM
// CHECK: mininpu.dram_alloc {{.*}} : memref<64x64xf16>
// 槽位发射序:A/B(f8,双缓冲)→ Cacc(f32)→ C(f16)→ sa/sb(len-1 → 16 元素
// 32B 对齐槽)
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x{{[0-9]+}}xf8E4M3FN, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x{{[0-9]+}}xf8E4M3FN, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x{{[0-9]+}}xf32, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x{{[0-9]+}}xf16, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<16xf16, 1>
// 一次性广播载入(先于 A/B prologue,槽位只写一次)
// CHECK: mininpu.dma_load {{.*}} : (memref<16xf16, strided<[0]>>, memref<16xf16, 1>)
// K 循环(tilesK=2):gemm_tile f8 A/B → f32 C 累加,第二条起 acc=1
// CHECK: mininpu.gemm_tile {{.*}}acc_mode = 0{{.*}}xf8E4M3FN{{.*}}xf8E4M3FN{{.*}}xf32
// CHECK: mininpu.gemm_tile {{.*}}acc_mode = 1{{.*}}xf8E4M3FN{{.*}}xf8E4M3FN{{.*}}xf32
// dequant:f32 源(GEMM_F8 的 C 块)+ 两个 (1,) scale 视图 → f16
// CHECK: mininpu.vec_dequant {{.*}}memref<{{[0-9]+}}x{{[0-9]+}}xf32{{.*}}memref<1xf16{{.*}}memref<1xf16{{.*}}memref<{{[0-9]+}}x{{[0-9]+}}xf16
// 回写 f16 输出
// CHECK: mininpu.dma_store {{.*}}memref<64x64xf16>

func.func @q_f8(%x: tensor<32x64xf16>, %sa: tensor<1xf16>) -> tensor<32x64xf8E4M3FN> {
  %y = minigraph.quant %x, %sa
       : (tensor<32x64xf16>, tensor<1xf16>) -> tensor<32x64xf8E4M3FN>
  return %y : tensor<32x64xf8E4M3FN>
}

// CHECK-LABEL: func.func @q_f8(
// CHECK: mininpu.dram_alloc {{.*}} : memref<32x64xf8E4M3FN>
// stride-0 广播视图 + 槽位(x/y f16/f8 双缓冲、len-1 sa 16 元素槽)
// CHECK: mininpu.view {{.*}} : memref<1xf16> -> memref<16xf16, strided<[0]>>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x64xf16, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x64xf8E4M3FN, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<16xf16, 1>
// 广播载入(sa 晚于 x,同通道) + vec_quant(x f16,sa (1,),y f8),
// wait = 本 tile x 的载入 token(len-1 时 sa token 无逐 tile 语义)
// CHECK: mininpu.dma_load {{.*}} : (memref<16xf16, strided<[0]>>, memref<16xf16, 1>)
// CHECK: mininpu.vec_quant {{.*}}x64xf16{{.*}}memref<1xf16{{.*}}x64xf8E4M3FN
// CHECK: mininpu.dma_store {{.*}}memref<32x64xf8E4M3FN>

// bf16:走 OP_GEMM 统一浮点路径(§6.6)——三元一致 bf16 memref
func.func @mm_bf16(%a: tensor<64x64xbf16>, %b: tensor<64x64xbf16>)
    -> tensor<64x64xbf16> {
  %y = minigraph.matmul %a, %b
       : (tensor<64x64xbf16>, tensor<64x64xbf16>) -> tensor<64x64xbf16>
  return %y : tensor<64x64xbf16>
}

// CHECK-LABEL: func.func @mm_bf16(
// CHECK: mininpu.dram_alloc {{.*}} : memref<64x64xbf16>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x{{[0-9]+}}xbf16, 1>
// CHECK: mininpu.gemm_tile {{.*}}xbf16{{.*}}xbf16{{.*}}xbf16
// CHECK: mininpu.dma_store {{.*}}memref<64x64xbf16>
