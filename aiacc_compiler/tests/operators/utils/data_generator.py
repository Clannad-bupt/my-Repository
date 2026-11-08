import numpy as np
from typing import Dict, List, Any

# 🛸 算子数学属性注册表 (Decoupled Specs)
# 每一个新算子只需在这里增加一行配置，无需修改生成器核心逻辑。
OP_DATA_SPECS = {
    # 默认配置 (Default)
    "default": {
        "range": (-50.0, 50.0),
        "seeds": [0.0, -0.0, np.nan, np.inf, -np.inf, 1e-10, -1e-10]
    },
    "sin": {
        "range": (-1.5707, 1.5707),
        "seeds": [0.0, -0.0, 1.5707, -1.5707, 0.7854, -0.7854, 1e-4, -1e-4]
    },
    "cos": {
        "range": (-1.5707, 1.5707),
        "seeds": [0.0, -0.0, 1.5707, -1.5707, 0.7854, -0.7854, 1e-4, -1e-4]
    },
    "add": {
        "range": (-50.0, 50.0),
        "seeds": [0.0, -0.0, 1.0, -1.0, 1e-5, -1e-5, np.nan, np.inf]
    },
    "sub": {
        "range": (-50.0, 50.0),
        "seeds": [0.0, -0.0, 1.0, -1.0, 1e-5, -1e-5, np.nan, np.inf]
    },
    "mul": {
        "range": (-10.0, 10.0),
        "seeds": [1.0, 0.0, -1.0, np.nan, np.inf]
    },
    "exp": {
        "range": (-1.0, 1.0),
        "seeds": [0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 0.1, -0.1, 1e-4, -1e-4]
    },
    "relu": {
        "range": (-20.0, 20.0),
        "seeds": [0.0, -0.0, 1e-5, -1e-5, 5.0, -5.0]
    },
    "rec": {
        "range": (0.1, 10.0),
        "seeds": [1.0, 0.1, 2.0, 0.5, 10.0, 1e-3]
    },
    "div": {
        "range": (0.1, 10.0),
        "seeds": [1.0, 0.1, 2.0, 0.5, 10.0, 1e-3]
    },
    "reciprocal": {
        "range": (0.5, 1.5),
        "seeds": [1.0, 0.5, 1.5, 0.8, 1.2, 0.99]
    },
    "and": {
        "range": (-100.0, 100.0),
        "seeds": [0.0, 1.0, -1.0, 
                  np.nan, np.inf,
                  np.frombuffer(b'\xff\xff\xff\xff', dtype=np.float32)[0], # All ones
                  np.frombuffer(b'\xaa\xaa\xaa\xaa', dtype=np.float32)[0], # Checkerboard
                  np.frombuffer(b'\x55\x55\x55\x55', dtype=np.float32)[0]] # Checkerboard
    },
    "or": {
        "range": (-100.0, 100.0),
        "seeds": [0.0, 1.0, -1.0, 
                  np.frombuffer(b'\xff\xff\xff\xff', dtype=np.float32)[0],
                  np.frombuffer(b'\x00\x00\x00\x00', dtype=np.float32)[0]]
    },
    "xor": {
        "range": (-100.0, 100.0),
        "seeds": [0.0, 1.0, -1.0, 
                  np.frombuffer(b'\xff\xff\xff\xff', dtype=np.float32)[0],
                  np.frombuffer(b'\xaa\xaa\xaa\xaa', dtype=np.float32)[0]]
    },
    "not": {
        "range": (-100.0, 100.0),
        "seeds": [0.0, 1.0, -1.0, 
                  np.frombuffer(b'\xff\xff\xff\xff', dtype=np.float32)[0]]
    },
    "sqrt": {
        "range": (0.1, 10.0),
        "seeds": [1.0, 0.1, 2.0, 0.5, 10.0, 1e-3, 0.0]
    },
    "rsqrt": {
        "range": (0.1, 10.0),
        "seeds": [1.0, 0.1, 2.0, 0.5, 10.0, 1e-3, 0.0]
    },
    "select_gt": {
        "range": (-50.0, 50.0),
        "seeds": [0.0, 1.0, -1.0, np.nan, 1234.5],
        "st_bits": [4, 0, 4, 0]
    },
    "select_eq": {
        "range": (-50.0, 50.0),
        "seeds": [0.0, 1.0, -1.0, np.nan, 1234.5],
        "st_bits": [2, 0, 2, 0]
    },
    "select_ls": {
        "range": (-50.0, 50.0),
        "seeds": [0.0, 1.0, -1.0, np.nan, 1234.5],
        "st_bits": [1, 0, 1, 0]
    },
    "atan2": {
        "range": (-10.0, 10.0),
        "seeds": [0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 10.0, -10.0]
    },
    "int8tofp16": {
        "range": (-128.0, 127.0),
        "seeds": [0, 127, -128, 1, -1]
    },
    "acos": {
        "range": (-0.8, 0.8),
        "seeds": [0.0, 0.5, -0.5, 0.707, -0.707]
    },
    "atan": {
        "range": (-0.8, 0.8),
        "seeds": [0.0, 0.5, -0.5, 0.2, -0.2]
    }
}

class ArithTestDataGenerator:
    """
    AIACC 算子测试数据工业级工厂 (V4.0 - Config Driven)。
    通过策略模式将算子数学特性与生成逻辑解耦。
    """
    
    @staticmethod
    def generate(op_type: str, mode: str = "fuzz", size: int = 4) -> Dict[str, np.ndarray]:
        spec = OP_DATA_SPECS.get(op_type, OP_DATA_SPECS["default"])
        
        if mode == "fuzz":
            return ArithTestDataGenerator._get_random_fuzz(op_type, spec, size)
        elif mode == "boundary":
            return ArithTestDataGenerator._get_boundary_sweep(op_type, spec, size)
        else:
            raise ValueError(f"Unsupported data mode: {mode}")

    @staticmethod
    def _get_random_fuzz(op_type: str, spec: dict, size: int) -> Dict[str, np.ndarray]:
        low, high = spec["range"]
        data = {
            f"ch_{i}": np.random.uniform(low, high, size).astype(np.float32)
            for i in range(4)
        }
        
        # 针对 Rec/Div 的关联初值特殊逻辑
        if op_type == "rec":
            with np.errstate(divide='ignore', invalid='ignore'):
                data["ch_1"] = (1.0 / data["ch_0"] + np.random.uniform(-0.01, 0.01, size)).astype(np.float32)
        elif op_type == "div":
            # Div: ch_0=x, ch_1=y, ch_2=y0_guess(1/y)
            with np.errstate(divide='ignore', invalid='ignore'):
                data["ch_2"] = (1.0 / data["ch_1"] + np.random.uniform(-0.01, 0.01, size)).astype(np.float32)
        elif op_type in ["sqrt", "rsqrt"]:
            # Sqrt/Rsqrt: ch_0=x, ch_1=y0_guess (1/sqrt(x))
            with np.errstate(divide='ignore', invalid='ignore'):
                data["ch_1"] = (1.0 / np.sqrt(data["ch_0"]) + np.random.uniform(-0.01, 0.01, size)).astype(np.float32)
        elif op_type.startswith("select"):
            # Select: ch_0=A, ch_1=B, ch_2=Cond_ST
            # 🛸 关键：ST 必须以 bit 形式存在于 UR 内存低位，不能是 float 含义
            st_choices = [0x0, 0x1, 0x2, 0x4, 0x7]
            data["ch_2"] = np.random.choice(st_choices, size).astype(np.uint32).view(np.float32)
        elif op_type == "atan2":
            # Atan2(y, x): ch_0=y, ch_1=x, ch_2=1/max(|x|,|y|)
            y, x = data["ch_0"], data["ch_1"]
            den = np.maximum(np.abs(x), np.abs(y))
            with np.errstate(divide='ignore', invalid='ignore'):
                data["ch_2"] = (1.0 / den + np.random.uniform(-0.01, 0.01, size)).astype(np.float32)
                data["ch_2"][np.isinf(data["ch_2"])] = 0.5
        elif op_type == "int8tofp16":
            # 🛸 特殊：INT8TOFP16 物理映射为 2x128-bit -> 4x128-bit
            # S0: 32x Lo-Nibble, S1: 32x Hi-Nibble
            i8_total = np.random.randint(-128, 127, size=(size, 32)).astype(np.int8)
            s0_rows, s1_rows = [], []
            for r in range(size):
                s0_word, s1_word = 0, 0
                for i in range(32):
                    v = int(i8_total[r, i]) & 0xFF
                    s0_word |= (v & 0xF) << (i * 4)
                    s1_word |= ((v >> 4) & 0xF) << (i * 4)
                s0_rows.append(np.array([(s0_word >> (k*32)) & 0xFFFFFFFF for k in range(4)], dtype=np.uint32).view(np.float32))
                s1_rows.append(np.array([(s1_word >> (k*32)) & 0xFFFFFFFF for k in range(4)], dtype=np.uint32).view(np.float32))
            data["ch_0"] = np.array(s0_rows)
            data["ch_1"] = np.array(s1_rows)
        
        return data

    @staticmethod
    def _get_boundary_sweep(op_type: str, spec: dict, size: int) -> Dict[str, np.ndarray]:
        seeds = spec["seeds"]
        
        def fill_vec(seed_list, offset, target_size):
            shifted_seeds = seed_list[offset % len(seed_list):] + seed_list[:offset % len(seed_list)]
            repeat_count = (target_size // len(shifted_seeds)) + 1
            return np.array((shifted_seeds * repeat_count)[:target_size]).astype(np.float32)

        data = {
            f"ch_{i}": fill_vec(seeds, i * 3, size) # 每个通道偏移 3 位
            for i in range(4)
        }

        if op_type == "rec":
            with np.errstate(divide='ignore', invalid='ignore'):
                data["ch_1"] = (1.0 / data["ch_0"]).astype(np.float32)
                mask = np.isnan(data["ch_1"]) | np.isinf(data["ch_1"])
                data["ch_1"][mask] = 0.5
        elif op_type == "div":
            with np.errstate(divide='ignore', invalid='ignore'):
                data["ch_2"] = (1.0 / data["ch_1"]).astype(np.float32)
                mask = np.isnan(data["ch_2"]) | np.isinf(data["ch_2"])
                data["ch_2"][mask] = 0.5
        elif op_type in ["sqrt", "rsqrt"]:
            with np.errstate(divide='ignore', invalid='ignore'):
                x_data = data["ch_0"].copy()
                x_data[x_data < 0] = 0.0
                data["ch_1"] = (1.0 / np.sqrt(x_data)).astype(np.float32)
                mask = np.isnan(data["ch_1"]) | np.isinf(data["ch_1"])
                data["ch_1"][mask] = 0.5
        elif op_type.startswith("select"):
            bits = spec.get("st_bits", [0])
            def fill_st(v_list, target_size):
                 return np.array((v_list * (target_size // len(v_list) + 1))[:target_size]).astype(np.uint32).view(np.float32)
            data["ch_2"] = fill_st(bits, size)
        elif op_type == "atan2":
            y, x = data["ch_0"], data["ch_1"]
            den = np.maximum(np.abs(x), np.abs(y))
            with np.errstate(divide='ignore', invalid='ignore'):
                data["ch_2"] = (1.0 / den).astype(np.float32)
                data["ch_2"][np.isinf(data["ch_2"])] = 0.5
        elif op_type == "int8tofp16":
            # 🛸 特殊：边界值扫描 (0, 127, -128)
            seeds_i8 = spec["seeds"]
            i8_total = np.array((seeds_i8 * (size * 32 // len(seeds_i8) + 1))[:size * 32]).reshape(size, 32)
            s0_rows, s1_rows = [], []
            for r in range(size):
                s0_word, s1_word = 0, 0
                for i in range(32):
                    v = int(i8_total[r, i]) & 0xFF
                    s0_word |= (v & 0xF) << (i * 4)
                    s1_word |= ((v >> 4) & 0xF) << (i * 4)
                s0_rows.append(np.array([(s0_word >> (k*32)) & 0xFFFFFFFF for k in range(4)], dtype=np.uint32).view(np.float32))
                s1_rows.append(np.array([(s1_word >> (k*32)) & 0xFFFFFFFF for k in range(4)], dtype=np.uint32).view(np.float32))
            data["ch_0"] = np.array(s0_rows)
            data["ch_1"] = np.array(s1_rows)

        return data
