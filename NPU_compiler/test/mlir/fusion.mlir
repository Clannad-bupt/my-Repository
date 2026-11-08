// fuse_matmul_bias_act 单元测试(README §8.1、§11.1)
// RUN 见 test/mlir/test_passes.py:minic --run-pass fusion 后 FileCheck
//
// 期望:silu(matmul) 与 silu(add(matmul,bias)) 各自融合成一条带 epilogue 的 matmul;
// 多消费者的 matmul 不融合(§8.1 合法性)。

func.func @fuse_silu(%a: tensor<32x256xf16>, %b: tensor<256x512xf16>) -> tensor<32x512xf16> {
  %0 = minigraph.matmul %a, %b : (tensor<32x256xf16>, tensor<256x512xf16>) -> tensor<32x512xf16>
  %1 = minigraph.silu %0 : tensor<32x512xf16> -> tensor<32x512xf16>
  return %1 : tensor<32x512xf16>
  // CHECK: minigraph.matmul %{{.*}}, %{{.*}} {epilogue = "silu"}
  // CHECK-NOT: minigraph.silu
}

func.func @fuse_bias_silu(%a: tensor<32x256xf16>, %b: tensor<256x512xf16>,
                          %bias: tensor<512xf16>) -> tensor<32x512xf16> {
  %0 = minigraph.matmul %a, %b : (tensor<32x256xf16>, tensor<256x512xf16>) -> tensor<32x512xf16>
  %1 = minigraph.add %0, %bias : (tensor<32x512xf16>, tensor<512xf16>) -> tensor<32x512xf16>
  %2 = minigraph.silu %1 : tensor<32x512xf16> -> tensor<32x512xf16>
  return %2 : tensor<32x512xf16>
  // CHECK: minigraph.matmul %{{.*}}, %{{.*}}, %{{.*}} {epilogue = "bias_silu"}
  // CHECK-NOT: minigraph.silu
  // CHECK-NOT: minigraph.add
}

func.func @no_fuse_multi_use(%a: tensor<32x256xf16>, %b: tensor<256x512xf16>)
    -> (tensor<32x512xf16>, tensor<32x512xf16>) {
  %0 = minigraph.matmul %a, %b : (tensor<32x256xf16>, tensor<256x512xf16>) -> tensor<32x512xf16>
  %1 = minigraph.silu %0 : tensor<32x512xf16> -> tensor<32x512xf16>
  return %0, %1 : tensor<32x512xf16>, tensor<32x512xf16>
  // CHECK: minigraph.matmul %{{.*}}, %{{.*}}
  // CHECK-NOT: epilogue
  // CHECK: minigraph.silu
}
