from typing import List, Dict, Tuple

class Conv2DGenerator:
    """
    AIACC V5.0 并发流水线卷积生成器。
    将计算任务分散到 4 个物理 IQ 上，实现极致交织。
    """
    def __init__(self, in_shape: Tuple[int, int, int], kernel: Tuple[int, int], stride: int = 1):
        self.C, self.H, self.W = in_shape
        self.KH, self.KW = kernel
        self.S = stride
        self.OH = (self.H - self.KH) // self.S + 1
        self.OW = (self.W - self.KW) // self.S + 1

    def generate_parallel_lir(self, ibank=0, wbank=1) -> List[Dict]:
        """
        生成 4 路并发指令流。
        IQ 0: 特征图读取 (7D)
        IQ 1: 权重读取 (3D)
        IQ 2: 乘法 (LOOPS)
        IQ 3: 加法 (LOOPS)
        """
        stream = []
        
        # 为了展示 Compactor 的威力，我们依旧在前端展开这些指令。
        # 在真实 HSTS 中，它们会表现为同步发生的四个 DFG 节点。
        total_steps = self.OH * self.OW * self.C * self.KH * self.KW
        
        # 1. 特征图读取流 (IQ 0)
        global_t = 0
        for oh in range(self.OH):
            for ow in range(self.OW):
                for ic in range(self.C):
                    for kh in range(self.KH):
                        for kw in range(self.KW):
                            in_h = oh * self.S + kh
                            in_w = ow * self.S + kw
                            in_addr = ic * (self.H * self.W) + in_h * self.W + in_w
                            stream.append({
                                "op_type": "UR_READ", "iq_id": 0,
                                "re": 1, "rbank": ibank, "raddr": in_addr,
                                "t": global_t
                            })
                            global_t += 1
                            
        # 2. 权重读取流 (IQ 1) - 它将与 IQ 0 并发
        global_t = 0
        for oh in range(self.OH):
            for ow in range(self.OW):
                for ic in range(self.C):
                    for kh in range(self.KH):
                        for kw in range(self.KW):
                            w_addr = kh * self.KW + kw
                            stream.append({
                                "op_type": "UR_READ", "iq_id": 1,
                                "re": 1, "rbank": wbank, "raddr": w_addr,
                                "t": global_t
                            })
                            global_t += 1
                            
        # 3. 计算流 (IQ 2 & 3) - 同样并发
        for t in range(total_steps):
            stream.append({"op_type": "FPMUL", "iq_id": 2, "t": t})
            stream.append({"op_type": "FPADD", "iq_id": 3, "t": t})
            
        return stream
