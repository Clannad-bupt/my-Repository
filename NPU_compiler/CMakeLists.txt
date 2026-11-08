cmake_minimum_required(VERSION 3.20)
project(mininpuc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# MLIR 21.1.8（Phase B，README §12.1 决策）。指向本机 LLVM 构建目录，例如：
#   cmake -B build -G Ninja \
#     -DMLIR_DIR=$HOME/llvm/build-mlir/lib/cmake/mlir \
#     -DLLVM_DIR=$HOME/llvm/build-mlir/lib/cmake/llvm
# 步骤 0/1（minisa 格式、simulator、runtime）不依赖 MLIR；
# 找不到 MLIR 时仅构建这些目标并警告。
find_package(MLIR CONFIG QUIET)
if(NOT MLIR_FOUND)
  message(WARNING "MLIR not found: only minisa/sim/runtime targets will be built. "
                  "Set -DMLIR_DIR/-DLLVM_DIR to enable dialects and passes.")
else()
  message(STATUS "Found MLIR ${MLIR_VERSION} at ${MLIR_DIR}")
  # 消费预编译 MLIR:需显式找 mlir-tblgen 并把 MLIR include 加进 tablegen 搜索路径
  find_program(MLIR_TABLEGEN_EXE mlir-tblgen PATHS ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH)
  if(NOT MLIR_TABLEGEN_EXE)
    message(FATAL_ERROR "mlir-tblgen not found under LLVM_TOOLS_BINARY_DIR=${LLVM_TOOLS_BINARY_DIR}")
  endif()
  list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}")
  include(TableGen)
  include(AddLLVM)
  include(AddMLIR)
  include_directories(${LLVM_INCLUDE_DIRS})
  include_directories(${MLIR_INCLUDE_DIRS})
  # 与 LLVM/MLIR 构建配置保持一致(否则链接缺 typeinfo/vtable)
  if(NOT LLVM_ENABLE_RTTI)
    add_compile_options(-fno-rtti)
  endif()
endif()

add_subdirectory(lib)
add_subdirectory(tools)
