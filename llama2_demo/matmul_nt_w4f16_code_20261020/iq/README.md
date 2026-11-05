# SMI_IQ IQ Abstraction Layer

这个目录放 **可复用** 的 SMI-IQ 软件侧抽象（工业界常见分层：通用队列层 / 模块子类层 / 算子层）。

- `smi_iq_base.*`：IQ 基类（通用：位宽检查、push 微指令、队列容量检查、后续可扩展 flush 到寄存器）
- `modules/`：各具体模块的 IQ 子类（INTADD/FPADD/FPMUL/UR/RtoVR...），负责自己的 CODE 位域打包

算子（OP）例如 `add` 不放在这里，它应该位于 `smi/SMI_IQ/op/`，并通过组合这些 IQ 子类完成 emit。
