# AIACC Compiler V5.0

## 新窗口上下文恢复 (分层加载，不要全读)

### 第 1 层：必读（2 个文件，120 行）
```
README.md                ← 你正在看的（入口）
docs/00_index.md         ← 目录结构 + 7 角色 + 文件速查
```

### 第 2 层：按任务只读对应文件

| 你要做什么 | 只读这些文件 |
|:--- |:--- |
| **硬件参数变更** | `.agents/skills/.../SKILL.md` + `hw_config/hw_params.yaml` |
| **新增算子** | `.agents/workflows/add-operator.md` |
| **开始某个 Sprint** | `docs/adm/sprint_plan.md` + 对应 `docs/tfd/phase{N}.md` |
| **写代码实现** | `docs/se/source_structure.md` + `docs/se/module_contracts.md` |
| **架构决策** | `docs/adm/adr/ADR-002*.md` + `docs/adm/adr/ADR-003*.md` |
| **检查遗漏** | `docs/hco/assumptions.md` |

### ⚠️ 不要一次读所有文件
docs/ 下有 29 个文件 (1639 行)，全读会浪费上下文。
只读第 1 层 + 当前任务对应的第 2 层文件即可。

---

## 给新窗口的提示语模板

复制以下内容发给新窗口：

```
请先读取以下文件恢复项目上下文：
1. /home/zhangjun/2_code/NPU/AIACC/aiacc_compiler/README.md
2. /home/zhangjun/2_code/NPU/AIACC/aiacc_compiler/docs/00_index.md

我当前要做的是：[你的任务]
请根据 README 中的任务映射表，只读取相关文件。
```

---

## 项目状态速查

- Git: `/home/zhangjun/2_code/NPU/AIACC/aiacc_compiler/`
- ADR: 001(Rtovr) + 002(Bank B+) + 003(Lowering)
- 角色: HCO → ADM → SE → TFD → IE → PVO → IG
- 工作流: `/refactor-aiacc` + `/add-operator`
- 当前阶段: 架构完成，准备 Sprint 1

## 常用命令

```bash
python3 build_report.py    # 生成设计报告
git log --oneline          # 查看进度
git tag                    # 查看里程碑
```
