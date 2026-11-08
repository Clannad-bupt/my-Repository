from typing import List, Dict, Optional, Any
from pydantic import BaseModel, Field

class HardwareParams(BaseModel):
    PARAM_SMC_NUM: int
    PARAM_SMC_VECTOR_WIDTH: int
    PARAM_IQ_DEPTH: int
    PARAM_OUTSTANDING_INST_NUM: int
    PARAM_EMBED_MAX_LEVEL: int
    PARAM_SMC_AXI_EN: int
    PARAM_UR_BANK_NUM: int
    PARAM_UR_BANK_DEPTH: int
    
    # 统合 ISA 头部参数
    ISA_HEADER_BITS: int = 41
    ISA_HEADER_FIELDS: Dict[str, int] = Field(default_factory=dict)

class ModuleField(BaseModel):
    name: str
    bits: int
    default: int = 0
    encoding: Dict[Any, Any] = Field(default_factory=dict)

class ModuleSpec(BaseModel):
    type: str
    latency: int
    inputs: List[str] = []   # 设为可选，增强容错性
    outputs: List[str] = []
    code_bits: int
    code_fields: List[ModuleField] = []

class IQConfig(BaseModel):
    module: str
    count: int = 1
    EN_LOOPS: bool = False
    BITS_LOOPS: int = 0
    EN_DLY: bool = False
    BITS_DLY: int = 0
    EN_INC: bool = False
    BITS_INC: int = 0
    EN_COND: bool = False
    BITS_COND: int = 0
    BITS_EMBED: int = 0
    BITS_EMBED_END: int = 0
    BITS_CODE: int = 0

class HardwareSpec(BaseModel):
    params: HardwareParams
    modules: Dict[str, ModuleSpec]
    iqs: Dict[str, IQConfig]
    delays: Dict[str, int]
    system_core: Optional[Dict[str, Any]] = None
    rtovr_config: Dict[str, Any] = {}

    @classmethod
    def load(cls, config_dir: str):
        import yaml
        from pathlib import Path
        base = Path(config_dir)
        
        def load_yaml(f):
            p = base / f
            if not p.exists(): return {}
            with open(p, "r", encoding='utf-8') as stream:
                return yaml.safe_load(stream)
        
        # 🛸 V2.4 大一统加载尝试 (SSoT)
        master_spec_path = base / "specs" / "aiacc_v2.4_master.yaml"
        master_data = None
        if master_spec_path.exists():
            with open(master_spec_path, "r", encoding='utf-8') as f:
                master_data = yaml.safe_load(f)
        
        # 1. 基础载入
        spec_data = {
            "params": load_yaml("hw_params.yaml")["hardware_params"],
            "modules": load_yaml("modules.yaml")["modules"],
            "iqs": load_yaml("iq_config.yaml")["iq_config"],
            "delays": load_yaml("pipeline_delays.yaml")["pipeline_delays"],
            "system_core": master_data["system_core"] if master_data else load_yaml("specs/system/system_core.yaml"),
            "rtovr_config": load_yaml("rtovr_config.yaml").get("rtovr_connectivity", {})
        }
        
        # 2. 深度加载每个模块的详细物理规格 (Specs Override Modules)
        modules_data = spec_data["modules"]
        for mname in modules_data:
            u_data = None
            if master_data and mname.upper() in master_data["functional_units"]:
                u_data = master_data["functional_units"][mname.upper()]
            else:
                # 回退到独立 YAML (保持兼容)
                spec_file = base / "specs" / "units" / f"{mname.lower()}.yaml"
                if spec_file.exists():
                    with open(spec_file, "r", encoding='utf-8') as f:
                        u_data = yaml.safe_load(f)
            
            if u_data:
                # 映射详细字段到 ModuleSpec 结构
                if "micro_instructions" in u_data:
                    fields = []
                    for f_info in u_data["micro_instructions"]:
                        fields.append({
                            "name": f_info.get("field", f_info.get("name")),
                            "bits": f_info.get("width", f_info.get("bits")),
                            "default": f_info.get("default", 0),
                            "encoding": f_info.get("encoding", {})
                        })
                    modules_data[mname]["code_fields"] = fields
                    modules_data[mname]["latency"] = u_data.get("latency", modules_data[mname]["latency"])

        return cls(**spec_data)

    @property
    def config(self):
        """兼容层：将 system_core 暴露为 config 字典，支持多层级穿透。"""
        if not self.system_core: return {}
        # 🛸 [FIX] 如果 system_core 内部还包了一层 system_core (某些 YAML 解析行为)
        if "system_core" in self.system_core:
            return self.system_core["system_core"]
        return self.system_core

    def get_iq_ids(self, unit_type: str) -> List[int]:
        """
        根据物理单元类型获取其下所有实例的 IQ ID。
        """
        registry = self.config.get("iq_registry", {})
        instances = self.config.get("resource_constraints", {}).get("unit_instances", {})
        
        # 🛸 [SMT-REFACTOR Step 1.1] 强行截断 RTOVR 槽位 (0-38) 避免分配到不存在的 DVR
        if unit_type.upper() == "RTOVR":
            base = registry.get("RTOVR", 100)
            return [base + i for i in range(39)]
        
        # 1. 处理 UR 特殊端口 (动态从 registry 映射地址)
        if unit_type.upper() == "UR_READ":
            base = registry.get("UR_READ", 30)
            count = self.config["resource_constraints"].get("total_read_ports", 4)
            return [base + i for i in range(count)]
        if unit_type.upper() == "UR_WRITE":
            base = registry.get("UR_WRITE", 35)
            count = self.config["resource_constraints"].get("total_write_ports", 4)
            return [base + i for i in range(count)]
            
        # 2. 处理 ALU 常规端口 (基于 Registry 偏移)
        if unit_type.upper() in instances:
            base_id = registry.get(unit_type.upper(), 0)
            count = instances[unit_type.upper()]
            return [base_id + i for i in range(count)]
        
        return []

    def get_iq_name(self, iq_id: int, unit_type: str = None) -> str:
        """
        反向寻址：强制使用 get_iq_ids 进行双向对齐，确保 100% 正确。
        """
        # 1. 如果提供了 unit_type，优先在其范围内查找
        if unit_type:
            ids = self.get_iq_ids(unit_type)
            if iq_id in ids:
                idx = iq_id - ids[0]
                u_u = unit_type.upper()
                if u_u in ["FPADD", "FPMUL", "INTADD", "LOGIC", "FPTOFP", "FPTOINT", "INTTOFP", "INTTOINT", 
                           "SHIFTUP", "SHIFTDN", "FP32ADDTREE5TO1", "FP32ADDTREE8TO1", "FP16MUL", "INT8TOFP16"]:
                    return u_u.lower()
                p_name = u_u.lower().replace("_read", "-r").replace("_write", "-w")
                if "-r" in p_name or "-w" in p_name:
                    return f"{p_name}{idx}"
                return f"{p_name}-{idx}"

        # 2. 全局扫描
        registry = self.config.get("iq_registry", {})
        for u_type in registry.keys():
            ids = self.get_iq_ids(u_type)
            if iq_id in ids:
                idx = iq_id - ids[0]
                u_u = u_type.upper()
                if u_u in ["FPADD", "FPMUL", "INTADD", "LOGIC", "FPTOFP", "FPTOINT", "INTTOFP", "INTTOINT", 
                           "SHIFTUP", "SHIFTDN", "FP32ADDTREE5TO1", "FP32ADDTREE8TO1", "FP16MUL", "INT8TOFP16"]:
                    return u_u.lower()
                p_name = u_u.lower().replace("_read", "-r").replace("_write", "-w")
                return f"{p_name}-{idx}"

        return f"{unit_type.lower()}_unknown" if unit_type else "unknown"

    def get_dvr_map(self) -> Dict[str, Dict[str, int]]:
        """从 rtovr_config 解析物理槽位(出口)绑定表: RtoVR -> Unit-Port"""
        raw_dvr = self.rtovr_config.get("producers_dvr", {})
        dvr_map = {}
        for slot_id, label in raw_dvr.items():
            parts = label.split('_')
            if len(parts) < 2: continue
            port = parts[-1]
            unit = "_".join(parts[:-1])
            if unit not in dvr_map: dvr_map[unit] = {}
            dvr_map[unit][port] = slot_id
        return dvr_map

    def get_dr_map(self) -> Dict[str, Dict[str, int]]:
        """从 rtovr_config 解析物理入口绑定表: Unit-Port -> RtoVR-SEL"""
        raw_dr = self.rtovr_config.get("consumers_dr", {})
        dr_map = {}
        for sel_id, label in raw_dr.items():
            parts = label.split('_')
            if len(parts) < 2: continue
            port = parts[-1]
            unit = "_".join(parts[:-1])
            if unit not in dr_map: dr_map[unit] = {}
            dr_map[unit][port] = sel_id
        return dr_map
