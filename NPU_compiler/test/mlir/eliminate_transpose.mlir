// eliminate_transpose 单元测试(README §5.2.4、§8)

func.func @double_transpose(%x: tensor<32x64xf16>) -> tensor<32x64xf16> {
  %0 = minigraph.transpose %x {permutation = [1, 0]} : tensor<32x64xf16> -> tensor<64x32xf16>
  %1 = minigraph.transpose %0 {permutation = [1, 0]} : tensor<64x32xf16> -> tensor<32x64xf16>
  return %1 : tensor<32x64xf16>
  // CHECK: func.func @double_transpose
  // CHECK-NOT: minigraph.transpose
  // CHECK: return
}

func.func @matmul_transpose_a(%a: tensor<256x32xf16>, %b: tensor<256x512xf16>) -> tensor<32x512xf16> {
  %0 = minigraph.transpose %a {permutation = [1, 0]} : tensor<256x32xf16> -> tensor<32x256xf16>
  %1 = minigraph.matmul %0, %b : (tensor<32x256xf16>, tensor<256x512xf16>) -> tensor<32x512xf16>
  return %1 : tensor<32x512xf16>
  // CHECK: func.func @matmul_transpose_a
  // CHECK-NOT: minigraph.transpose
  // CHECK: minigraph.matmul
  // CHECK-SAME: transpose_a = true
}

func.func @matmul_transpose_b_cancel(%a: tensor<32x256xf16>, %b: tensor<256x512xf16>) -> tensor<32x512xf16> {
  // 显式 transpose(%b→512x256) + transpose_b=true(等效再转回)= 两次抵消
  %0 = minigraph.transpose %b {permutation = [1, 0]} : tensor<256x512xf16> -> tensor<512x256xf16>
  %1 = minigraph.matmul %a, %0 {transpose_b = true}
       : (tensor<32x256xf16>, tensor<512x256xf16>) -> tensor<32x512xf16>
  return %1 : tensor<32x512xf16>
  // CHECK: func.func @matmul_transpose_b_cancel
  // CHECK-NOT: minigraph.transpose
  // CHECK: minigraph.matmul %{{.*}}, %{{.*}} :
  // CHECK-NOT: transpose_b
}
