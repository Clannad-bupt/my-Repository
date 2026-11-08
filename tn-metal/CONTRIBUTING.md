# tn-metal 

tn-metal：ThinkNative NPU 的 Metal 层 SDK（宿主 runtime / driver，设备 ap / scalar / ops / kernel）。
本指南描述提交规范与开发工作流。

## 环境搭建

```bash
# 构建（一个 build 目录固定一个芯片；默认 TN_PSEUDO_CODE=OFF 真机路径，
# 编译 driver / lpu_rt）
cmake -B build && cmake --build build

# 伪代码模拟（host 模拟设备路径，不编译 driver / lpu_rt）
cmake -B build -DTN_PSEUDO_CODE=ON && cmake --build build

# 运行冒烟测试
./build/src/kernel/smoke_ref

# 安装提交门禁 hook（每人一次性操作；clang-format 与上游镜像 hook 同版本二进制）
pip install pre-commit
pip install clang-format==19.1.0
pre-commit install --hook-type pre-commit --hook-type commit-msg
```

## 提交信息规范

格式：`<type>(<scope>): <subject>`，header 与 body 之间空一行。

```
feat(kernel): add vec mul ref impl

- support i8/i16/fp16 dtypes
- reuse ldst tensor view infra
```

### type（7 个，封闭枚举）

| type | 含义 |
|---|---|
| `feat` | 新功能 |
| `fix` | 修 bug |
| `refactor` | 不改行为的重构 |
| `perf` | 性能优化 |
| `test` | 测试 |
| `docs` | 文档 |
| `chore` | 杂务（.gitignore / CMakeLists / 编译选项等） |

### scope（6 个代码模块，纯位置轴）

| scope | 覆盖范围 |
|---|---|
| `kernel` | src/kernel/** + include/kernel/**（含 tdr.h） |
| `driver` | src/driver/** + include/driver/** |
| `ops` | src/ops/** + include/ops/** |
| `runtime` | src/runtime/** + include/runtime/** |
| `scalar` | src/scalar/** + include/scalar/** |
| `ap` | src/ap/** |

规则：

- scope 是纯位置轴，只映射代码模块目录；性质由 type 表达；
- 测试改动：scope = 被测模块（`test(kernel): ...`）；跨模块测试基建无 scope（`test: ...`）；
- 文档改动：scope = 被文档化的模块（`docs(kernel): ...`）；顶层 README 等无 scope（`docs: ...`）；
- 杂务（CMake / .gitignore）无 scope：`chore: enable -Wall in CMakeLists`。

### subject / body / footer

- subject：英文祈使句，≤72 字符，不加句号；
- body：解释改动，`-` bullet 列表；
- footer（可选）：`Refs: <id>` 关联 issue；`Fixes: <id>` 修复并关闭；`Co-authored-by:` 多人归属；破坏性变更用 `BREAKING CHANGE: <说明>`。

### 跨模块改动

1. 各模块改动独立 → `git add <path>` 逐模块拆分提交；
2. 拆分前提是每个 commit 都能独立编译（bisect 纪律）；被依赖方先提交；
3. 接口联动 → scope = 驱动改动的模块


## 代码规范

- 格式化由 `.clang-format` 定义（LLVM 基线，4 空格缩进，100 列），pre-commit hook 自动改写；
- 代码与注释（含字符串字面量）**禁止中文**，仅 `.md` 文档允许中文——`no-cjk-in-code` hook 强制。

## 工作流（Phase 1）

当前阶段允许 main 直推，但：

- 所有 commit 必须通过本地 hook（提交信息 + 格式 + 语言检查）；
- 每人对自己直推的 commit 负责；