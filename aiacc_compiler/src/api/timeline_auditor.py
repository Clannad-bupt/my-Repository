import re
from typing import Dict, List, Any

class TimelineAuditor:
    """
    AIACC 物理时序审计仪 (The Pipeline Auditor)
    用于比对: [算法预测时序] vs [仿真器实测轨迹]
    """
    def __init__(self):
        self.report = []

    def audit_cycle_by_cycle(self, algo_data_raw: Dict[str, int], sim_trace_path: str):
        """
        [算法对位审计] 逐周期对比算法预期与硬件仿真表现。
        """
        # 1. 解析仿真生成的轨迹
        sim_data = self._parse_simulation_trace(sim_trace_path)
        
        # 2. 预处理算法数据：将 {node: cycle} 翻转为 {cycle: [nodes]}
        algo_data = {}
        for node, cyc in algo_data_raw.items():
            if cyc not in algo_data: algo_data[cyc] = []
            algo_data[cyc].append(node)
            
        # 3. 交叉对比逻辑
        self.report.append(f"{'周期 (Cycle)':<10} | {'算法预期节点 (Algo Pred)':<25} | {'仿真运行单元 (Sim Actual)':<25} | {'核查结果':<10}")
        self.report.append("-" * 85)
        
        # 寻找最大周期轴
        algo_keys = list(algo_data.keys())
        sim_keys = list(sim_data.keys())
        max_cyc = max(max(algo_keys) if algo_keys else 0, max(sim_keys) if sim_keys else 0)
        
        for cyc in range(max_cyc + 1):
            a_nodes = algo_data.get(cyc, [])
            s_units = sim_data.get(cyc, [])
            
            # 使用简单的集合逻辑判断在该拍是否有动作
            match = "【对齐】" if bool(a_nodes) == bool(s_units) else "⚠️【偏差】"
            
            self.report.append(f"{cyc:<10} | {str(a_nodes):<25} | {str(s_units):<25} | {match:<10}")
            
        return "\n".join(self.report)

    def _parse_simulation_trace(self, path: str):
        trace_data = {}
        with open(path, "r") as f:
            lines = f.readlines()
            for line in lines:
                if "|" not in line or "iq_name" in line: continue
                parts = line.split("|")
                unit = parts[0].strip()
                grid = parts[1].strip().split()
                for cyc, symbol in enumerate(grid):
                    if symbol != ".":
                        if cyc not in trace_data: trace_data[cyc] = []
                        trace_data[cyc].append(f"{unit}:{symbol}")
        return trace_data

class PerformanceAnalyzer:
    """
    AIACC 性能瓶颈诊断与提升引擎 (The Efficiency Insights)
    """
    def calculate_theoretical_bound(self, dfg_ops: List[Any], hardware_latencies: Dict[str, int]):
        """
        [分析模型] 计算 DFG 的关键路径理论极限。
        """
        max_depth = 0
        for op in dfg_ops:
            depth = op.metadata.get("depth", 1)
            unit_type = op.node_id.split("_")[0]
            lat = hardware_latencies.get(unit_type, 1)
            # 这里简化为深度累加时延
            max_depth = max(max_depth, (depth * lat))
        
        return int(max_depth)

    def analyze_efficiency_gap(self, theoretical: int, actual: int):
        """
        横跳比对：理论极限 vs 实际运行
        """
        gap = actual - theoretical
        loss_ratio = (gap / actual) * 100 if actual > 0 else 0
        
        analysis = [
            f"--- AIACC 算子性能效能诊断报告 (SOP Edition) ---",
            f"1. 理论极致性能周期 (Theoretical Bound): {theoretical}",
            f"2. 仿真实际运行周期 (Actual Latency): {actual}",
            f"3. 性能丢失间隙 (Efficiency Gap)   : {gap} 周期 ({loss_ratio:.2f}%)",
            f"",
            f"--- 性能提升建议 (Performance Improvement Insights) ---",
        ]
        
        if gap > 15:
            analysis.append("🚨 [高优先级优化]: 检测到显著的流水线空洞 (Bubble)！")
            analysis.append("   - 优化点 A: 指令融合 (Fusion)。检查 UR 读写是否可以合入计算内部，消除搬运开销。")
            analysis.append("   - 优化点 B: 隐藏访存延迟 (Latency Hiding)。现代硬件（如 Google TPU）通过在计算间隙点火 UR 指令来掩盖延迟。")
            analysis.append("   - 优化点 C: 消除 Bank 冲突。检查算法合成层是否在同一拍请求了同一个物理 Bank。")
        elif gap > 0:
            analysis.append("⚖️ [中优先级优化]: 时序已接近物理极限，仍有微调空间。")
            analysis.append("   - 优化点: 寄存器文件对齐。减少中间变量回写 Bank 的次数。")
        else:
            analysis.append("✅ [极致性能]: 实际运行已对齐理论极限。物理时序非常紧致。")
            
        analysis.append(f"\n[参考]: 现代高性能硬件设计理念 - 通过多发射 (Multi-issue) 和 静态乱序调度 (Static Scheduling) 极致填补流水线空洞。")
            
        return "\n".join(analysis)
