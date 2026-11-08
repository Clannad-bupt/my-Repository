from abc import ABC, abstractmethod
from typing import List, Dict, Any, Set
import numpy as np

class BaseOperator(ABC):
    @property
    def element_bits(self) -> int:
        dfg = self.get_dfg()
        for node in dfg:
            p = node.metadata.get("precision") or node.metadata.get("element_bits")
            if p:
                if isinstance(p, int): return p
                p = str(p).lower()
                if "32" in p: return 32
                if "16" in p: return 16
                if "8" in p: return 8
                if "4" in p: return 4
        return 32 

    @property
    def dtype(self):
        """根据位宽自动映射 NumPy 类型"""
        bits = self.element_bits
        if bits == 32: return np.float32
        if bits == 16: return np.float16
        return np.float32

    @property
    @abstractmethod
    def tensor_configs(self) -> Dict[str, Dict[str, Any]]:
        pass

    @abstractmethod
    def get_dfg(self, spec: Any = None) -> List[Any]:
        pass

    @abstractmethod
    def get_golden(self, input_data: Dict[str, np.ndarray], vector_lanes: int) -> Dict[str, Any]:
        pass

    @abstractmethod
    def prepare_input(self, n_rows: int) -> Dict[str, np.ndarray]:
        pass

    def _get_hw_param(self, spec: Any, key: str, default: int) -> int:
        if not spec or not hasattr(spec, "params"): return default
        return getattr(spec.params, key, default)

    def get_bank_addr(self, name: str, spec: Any) -> int:
        bank_depth = self._get_hw_param(spec, "PARAM_UR_BANK_DEPTH", 256)
        config = self.tensor_configs.get(name)
        if not config:
            raise ValueError(f"Tensor '{name}' not found in tensor_configs.")
        bank_val = config["bank"][0] if isinstance(config["bank"], list) else config["bank"]
        return bank_val * bank_depth + config.get("offset", 0)

    def _find_free_bank(self, spec: Any) -> int:
        used_banks = set()
        for cfg in self.tensor_configs.values():
            if "bank" in cfg:
                if isinstance(cfg["bank"], list):
                    used_banks.update(cfg["bank"])
                else:
                    used_banks.add(cfg["bank"])
        max_banks = self._get_hw_param(spec, "PARAM_UR_BANK_NUM", 8)
        for b in range(max_banks):
            if b not in used_banks:
                return b
        raise RuntimeError("No free UR banks available for intermediate storage.")

    def build_inputs_meta(self, symbols_map: Dict[str, Any], spec: Any) -> Dict:
        meta = {}
        for name, symbol in symbols_map.items():
            addr_base = self.get_bank_addr(name, spec)
            config = self.tensor_configs[name]
            default_stride = 1 if config.get("role") == "input" else 0
            stride = config.get("stride", default_stride)
            
            # 提取基础元数据
            meta_data = {
                "logical_addr": addr_base, 
                "stride": stride, 
                "role": config.get("role", "operand")
            }
            
            # [Phase 4] 提取 YAML 中用户自定义的所有其它扩展属性 (如 shape, cast_to, loop_bind)
            for k, v in config.items():
                if k not in ["bank", "offset", "stride", "role"]:
                    meta_data[k] = v

            if isinstance(symbol, list):
                for i, sym in enumerate(symbol):
                    meta_i = dict(meta_data)
                    meta_i["logical_addr"] = addr_base + i
                    meta[sym] = meta_i
            else:
                meta[symbol] = meta_data
        return meta

    def compile_with_adpc(self, formula, inputs_meta: Dict, out_name: str = "y", out_addr: int = None, spec: Any = None) -> List[Any]:
        from src.frontend.compiler import GraphCompiler
        from src.core.memory_layout import PhysicalMemoryLayout
        from src.frontend.optimizer import PhysicalGraphOptimizer

        # [Phase 2] 构建统一地址仲裁者
        layout = PhysicalMemoryLayout.from_spec(spec)

        # 登记所有已知 tensor（输入、输出、常数）
        for name, cfg in self.tensor_configs.items():
            if "bank" in cfg:
                banks = cfg["bank"] if isinstance(cfg["bank"], list) else [cfg["bank"]]
                for idx, b in enumerate(banks):
                    t_name = name if idx == 0 else f"{name}_{idx}"
                    layout.declare_tensor(
                        name=t_name,
                        bank=b,
                        offset=cfg.get("offset", 0),
                        role=cfg.get("role", "input")
                    )

        # scratch bank 选择第一个未被占用的 bank
        scratch_bank = layout.find_free_bank()
        layout._scratch_bank = scratch_bank

        # 将 layout 传入 optimizer（optimizer 内部使用 layout.allocate_scratch）
        bank_depth = layout.bank_depth
        optimizer = PhysicalGraphOptimizer(
            scratch_bank=scratch_bank,
            bank_depth=bank_depth,
            layout=layout
        )
        
        # 🛸 [SMT-REFACTOR Step 1.3] 纯标量图 (stride 均为 0) 直接豁免 Spill，降维 DFG
        all_scalars = all(meta.get("stride", 1) == 0 for meta in inputs_meta.values())
        if all_scalars:
            optimizer.disable_spill = True

        self.compiler = GraphCompiler(optimizer=optimizer)
        ops = self.compiler.compile(inputs_meta, formula, out_name=out_name, spec=spec)

        sym_to_addr = {}
        for op in ops:
            role = op.metadata.get("role")
            if role == "intermediate" and op.op_type in ["UR_WRITE", "UR_READ"]:
                # optimizer 已通过 layout 写入 logical_addr，这里只做同步检查
                if "logical_addr" not in op.metadata:
                    # 回退：以前的逻辑
                    if op.op_type == "UR_WRITE":
                        sym_name = op.trace_id.replace("spill_", "")
                    else:
                        spill_node_id = op.metadata.get("depends_on", [None])[0]
                        sym_name = spill_node_id.replace("spill_", "") if spill_node_id else "anon"
                    if sym_name not in sym_to_addr:
                        sym_to_addr[sym_name] = layout.allocate_scratch(sym_name)
                    op.metadata["logical_addr"] = sym_to_addr[sym_name]
                op.metadata["stride"] = 1

            elif out_addr is not None and op.op_type == "UR_WRITE" and op.metadata.get("role") == "output":
                # 🛸 [ALGORITHM OPTIMIZATION] 支持多输出/矩阵算子：自动提取输出节点索引（如 out_obj_i），为其分派偏移地址 out_addr + i
                idx = 0
                if op.trace_id.startswith("out_obj_"):
                    try:
                        idx = int(op.trace_id.split("_")[-1])
                    except ValueError:
                        pass
                op.metadata["logical_addr"] = out_addr + idx
                op.metadata["stride"] = 1
                op.metadata["is_output"] = True

            if op.op_type.upper() == "RTOVR":
                prod_ids = op.metadata.get("depends_on", [])
                if prod_ids:
                    prod_id = prod_ids[0]
                    for consumer in ops:
                        if op.trace_id in consumer.metadata.get("depends_on", []):
                            consumer.metadata.setdefault("rtovr_deps", {})[prod_id] = {
                                "dvr": op.metadata.get("dvr", -1),
                                "sel": op.metadata.get("sel", -1)
                            }

        # 编译期冲突检测——小问题将报警而非崩溃
        conflicts = layout.validate_no_overlap()
        if conflicts:
            import warnings
            for c in conflicts:
                warnings.warn(f"[MemoryLayout] {c}", stacklevel=2)

        self.ops = ops
        return ops

    def inject_data(self, program: Any, input_data: Dict[str, np.ndarray], vector_lanes: int, spec: Any = None):
        for name, config in self.tensor_configs.items():
            if config["role"] == "output": continue
            addr_base = self.get_bank_addr(name, spec)
            data = input_data.get(name)
            if data is None: continue
            if config["role"] == "input":
                default_stride = 1 if config.get("role") == "input" else 0
                stride = config.get("stride", default_stride)
                program.inject_vector(addr_base, data, stride=stride)
            elif config["role"] == "param":
                if isinstance(data, (list, np.ndarray)):
                    for i, val in enumerate(data):
                        program.inject_broadcast(addr_base + i, val)
                else:
                    program.inject_broadcast(addr_base, data)
