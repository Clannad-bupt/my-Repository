from typing import List, Dict, Tuple

class Compactor:
    """
    AIACC 极致时空压缩引擎 (v8: 多维张量感知版)。
    支持硬件级 7 层循环嵌套 (EMBED 0-6)。
    """
    @staticmethod
    def compress_iq_stream(stream: List[Tuple[int, int, bool]], iq_conf) -> List[Dict]:
        # Step 1: 1D 基础折叠 (最内层)
        packets = Compactor._pass1_linear_folding(stream, iq_conf)
        return packets
            
        # Step 3: 根据探测到的 Chain 发射 Header 序列
        # dim_chain 格式: [(loops, stride, depth), ...]
        res = []
        for loops, stride, depth in reversed(dim_chain):
            # 只有 depth > 0 的才是 Header
            if depth > 0:
                header = {
                    "t": packets[0]["t"],
                    "payload": 0,
                    "loops": loops,
                    "dly": 0,
                    "inc": stride,
                    "embed": depth,
                    "embed_end": 0
                }
                res.append(header)
        
        # 最后加上 Body (最内层指令)
        body = packets[0].copy()
        # 设置 EMBED_END 掩码 (所有探测到的维度都在此结束)
        mask = 0
        for _, _, depth in dim_chain:
            if depth > 0:
                mask |= (1 << (depth - 1))
        body["embed_end"] = mask
        res.append(body)
        
        return res

    @staticmethod
    def _detect_strides(packets: List[Dict]) -> List[Tuple[int, int, int]]:
        """
        探测指令包序列中的 N 维步进规律。
        返回: [(count_dim0, stride_dim0, 0), (count_dim1, stride_dim1, 1), ...]
        """
        # 我们假设最内层 (d0) 已经通过 1D 折叠成了一个 packet 的 loops
        # 现在我们要看这个 packet 是不是在重复。
        # 简化版：我们假设整个序列是一个完美的 N-D 张量
        chain = []
        
        # 1. 记录最内层 (EMBED 0)
        # 注意：d0 的 loops 是由 Pass1 计算出来的。
        # 这里记录的是它“自身”的次数，其实不需要记入 chain 的升维部分，直接体现在 Body 即可。
        
        curr_seq = packets
        depth = 1
        while len(curr_seq) > 1 and depth <= 6:
            # 寻找序列的最小循环节
            # 如果序列整体能被平分为 N 份，且每份全等（payload 呈 stride 变化）
            found_dim = False
            for factor in range(2, len(curr_seq) + 1):
                if len(curr_seq) % factor == 0:
                    block_size = len(curr_seq) // factor
                    pattern = curr_seq[:block_size]
                    stride = curr_seq[block_size]["payload"] - pattern[0]["payload"]
                    
                    # 校验全等性
                    match = True
                    for k in range(1, factor):
                        start = k * block_size
                        for offset in range(block_size):
                            if curr_seq[start+offset]["payload"] != pattern[offset]["payload"] + stride * k or \
                               curr_seq[start+offset]["loops"] != pattern[offset]["loops"]:
                                match = False; break
                        if not match: break
                    
                    if match:
                        # 命中了！将此维度记录
                        if factor > 1: chain.append((factor - 1, stride, depth))
                        curr_seq = pattern # 缩减到上一层继续识别
                        depth += 1
                        found_dim = True
                        break
            if not found_dim: break
            
        return chain

    @staticmethod
    def _pass1_linear_folding(stream, iq_conf) -> List[Dict]:
        """与 V5/V6/V7 逻辑一致。兼容 tuple 和 dict 输入。"""
        if not stream: return []
        
        # 归一化：全量保留 dict 信息，不再裁剪为 tuple
        normalized = []
        for item in stream:
            if isinstance(item, dict):
                normalized.append(item)
            else:
                normalized.append({"t": item[0], "payload": item[1], "valid": 1})
        stream = normalized
        
        res = []
        i = 0
        next_expected_t = 0 # 🛰️ 全局物理对位点
        max_loops = (1 << iq_conf.BITS_LOOPS) - 1 if iq_conf.EN_LOOPS else 0
        
        while i < len(stream):
            curr_item = stream[i]
            t_curr = curr_item["t"]
            p_curr = curr_item["payload"]
            
            # --- 🛸 核心修复：物理气泡注入 (Gap Filling) ---
            while next_expected_t < t_curr:
                gap = t_curr - next_expected_t
                curr_loops = min(gap, max_loops + 1) - 1
                res.append({
                    "t": next_expected_t, 
                    "payload": 0, 
                    "loops": curr_loops, "dly": 0, "inc": 0, 
                    "embed": 0, "embed_end": 0,
                    "valid": 0
                })
                next_expected_t += (curr_loops + 1)
            
            loops, stride = 0, 0
            # 👽 显式优先原则：如果指令本身已经自带了循环属性，则优先采用
            if curr_item.get("loops", 0) > 0 or curr_item.get("embed", 0) > 0 or curr_item.get("level", 0) > 0:
                loops = curr_item.get("loops", 0)
                stride = curr_item.get("inc", 0)
            elif iq_conf.EN_INC and i + 1 < len(stream):
                t_next, p_next = stream[i+1]["t"], stream[i+1]["payload"]
                if t_next == t_curr + 1:
                    stride = p_next - p_curr
                    if abs(stride) < (1 << (iq_conf.BITS_INC - 1)):
                        j = i + 1
                        while j < len(stream) and loops < max_loops:
                            t_j, p_j = stream[j]["t"], stream[j]["payload"]
                            if t_j == t_curr + (j - i) and p_j == p_curr + stride * (j - i):
                                loops += 1; j += 1
                            else: break
            
            # 兜底：如果没手动指定也没探测到 stride，尝试普通 loops 探测
            if loops == 0 and iq_conf.EN_LOOPS:
                j = i + 1
                while j < len(stream) and loops < max_loops:
                    if stream[j]["t"] == t_curr + (j - i) and stream[j]["payload"] == p_curr:
                        loops += 1; j += 1
                    else: break
            
            # 🛸 V5.2 极致升级：自适应 IQ 级循环缝合 (Automated IQ Loop Stitching)
            # 逻辑：对于任何 IQ 序列，若元数据中带有指定的 outer_loops，则在起点插入 Header，在终点插入 Footer。
            
            # 1. 起点：插入针对外层循环 (Level 1) 的 LOOP_HDR
            # 硬件 Header 字段映射（来自 hw_spec iq_header 规格）:
            #   INC_EMBED ← embed=0   (无嵌套基准增量)
            #   INC       ← inc=outer_inc  (地址步进)
            #   DLY       ← dly=0
            #   LOOPS     ← loops=outer_loops
            #   EMBED_LOOPS ← level=1  (Loop 控制器层级)
            #   VLD=0 → 纯控制指令，硬件不执行为实际 UR_READ
            if i == 0 and curr_item.get("outer_loops", 0) > 0:
                header = {
                    "t": t_curr, "payload": 0,
                    "loops": curr_item.get("outer_loops"),
                    "dly": 0,
                    "inc":   curr_item.get("outer_inc", 0),  # → INC 字段
                    "embed": 0,                              # → INC_EMBED=0
                    "level": 1,                              # → EMBED_LOOPS=1
                    "embed_end": 0,
                    "valid": 0,   # 🛸 关键：VLD=0 → 纯控制，不执行为真实读写
                }
                res.append(header)
            
            # 2. 生成 Body (带内层 loop)
            # 计算是否为当前 IQ 序列的最后一个 folded packet
            is_last = (i + loops + 1 == len(stream))
            
            cmd = {
                "t": t_curr, "payload": p_curr, 
                "loops": loops or curr_item.get("inner_loops", 0), 
                "dly": curr_item.get("dly", 0), 
                "inc": stride or curr_item.get("inc", 0), 
                "embed": 0, "level": 0,
                "embed_end": 1 if (is_last and curr_item.get("outer_loops", 0) > 0) else 0,
                "valid": curr_item.get("valid", 1)
            }
            res.append(cmd)
            
            i += (loops + 1)
            next_expected_t = t_curr + (loops + 1)
            
            
        print(f"[COMPA-DEBUG] Stream len={len(stream)} -> Pass1 folded len={len(res)}")
        return res
