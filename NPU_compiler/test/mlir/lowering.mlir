// lowering 单元测试:matmul(带 bias_silu epilogue)模板展开 + SRAM 静态分配
// RUN 路径见 test/mlir/test_passes.py(lowering 用 --run-pass minigraph-lower-to-mininpu,默认 spec)

// K=512 > Tk 上限 256 → k 循环 2 次迭代,覆盖 acc_mode=0/1 两条路径
func.func @mm_bias_silu(%a: tensor<64x512xf16>, %b: tensor<512x64xf16>, %bias: tensor<64xf16>) -> tensor<64x64xf16> {
  %0 = minigraph.matmul %a, %b, %bias {epilogue = "bias_silu"}
       : (tensor<64x512xf16>, tensor<512x64xf16>, tensor<64xf16>) -> tensor<64x64xf16>
  return %0 : tensor<64x64xf16>
  // CHECK: func.func @mm_bias_silu
  // CHECK-SAME: memref<64x64xf16>
  // 双缓冲:A/B 各两组槽位 + C 一组(§9.4)
  // CHECK: mininpu.sram_alloc
  // CHECK: mininpu.dma_load
  // CHECK: mininpu.sync
  // CHECK: mininpu.gemm_tile
  // CHECK-SAME: acc_mode = 0
  // CHECK: mininpu.gemm_tile
  // CHECK-SAME: acc_mode = 1
  // bias epilogue 走 V_ADD 广播,silu 走 V_SILU
  // CHECK: mininpu.vec_add
  // CHECK: mininpu.vec_silu
  // CHECK: mininpu.dma_store
  // CHECK: return
  // CHECK-NOT: minigraph.matmul
}
