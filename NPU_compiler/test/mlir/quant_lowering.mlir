// 二期-INT8(README §6.5):quant / quant_matmul → mininpu 模板展开 FileCheck。
// 关注:i8/i32 memref 槽位、gemm_tile(i8→i32)、vec_quant/vec_dequant 的
// 第 4 操作数 sa、双击 buffer 预取与 sync 结构。
// RUN 方式见 test/mlir/test_passes.py(CASES 表)。

func.func @qmm(%a: tensor<64x512xi8>, %b: tensor<512x64xi8>,
               %sa: tensor<64xf16>, %sb: tensor<64xf16>) -> tensor<64x64xf16> {
  %y = minigraph.quant_matmul %a, %b, %sa, %sb
       : (tensor<64x512xi8>, tensor<512x64xi8>, tensor<64xf16>, tensor<64xf16>)
         -> tensor<64x64xf16>
  return %y : tensor<64x64xf16>
}

// CHECK-LABEL: func.func @qmm(
// 输出 buffer:f16 DRAM(空间 0 打印省略)
// CHECK: mininpu.dram_alloc {{.*}} : memref<64x64xf16>
// 槽位发射序:A/B(i8,双缓冲)→ C32(i32)→ C(f16)→ sa/sb(1-D f16)
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x{{[0-9]+}}xi8, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x{{[0-9]+}}xi32, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x{{[0-9]+}}xf16, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}xf16, 1>
// K 循环(tilesK=2):gemm_tile i8 A/B memref → i32 C,第二条起 acc=1
// CHECK: mininpu.gemm_tile {{.*}}acc_mode = 0{{.*}}xi8{{.*}}xi8{{.*}}xi32
// CHECK: mininpu.gemm_tile {{.*}}acc_mode = 1{{.*}}xi8{{.*}}xi8{{.*}}xi32
// dequant:i32 → f16,带 sb/sa 两个 scale
// CHECK: mininpu.vec_dequant {{.*}}xi32{{.*}}xf16{{.*}}xf16{{.*}}xf16
// 回写 f16 输出
// CHECK: mininpu.dma_store {{.*}}memref<64x64xf16>

func.func @q(%x: tensor<32x64xf16>, %sa: tensor<32xf16>) -> tensor<32x64xi8> {
  %y = minigraph.quant %x, %sa
       : (tensor<32x64xf16>, tensor<32xf16>) -> tensor<32x64xi8>
  return %y : tensor<32x64xi8>
}

// CHECK-LABEL: func.func @q(
// CHECK: mininpu.dram_alloc {{.*}} : memref<32x64xi8>
// x 槽 f16、y 槽 i8、sa 槽 f16 1-D(发射序)
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x64xf16, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}x64xi8, 1>
// CHECK: mininpu.sram_alloc {{.*}} : memref<{{[0-9]+}}xf16, 1>
// CHECK: mininpu.vec_quant {{.*}}x64xf16{{.*}}xf16{{.*}}x64xi8
// CHECK: mininpu.dma_store {{.*}}memref<32x64xi8>
