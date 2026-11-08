import yaml
import os
from dataclasses import dataclass
from typing import Dict, List, Any, Optional

class Bunch(object):
    """一个简单的将字典转换为对象访问风格的包装类，并支持 .get()"""
    def __init__(self, adict):
        self._data = adict
        self.__dict__.update({
            k: Bunch(v) if isinstance(v, dict) else v 
            for k, v in adict.items()
        })
    def __getattr__(self, name):
        return None  # 防止访问不存在的属性时抛出异常
    def get(self, key, default=None):
        return self.__dict__.get(key, default)

@dataclass
class MicroInstructionField:
    name: str
    width: int
    default: Optional[int] = None
    encoding: Optional[Dict[int, str]] = None

@dataclass
class PortSpec:
    name: str
    width: int
    desc: str = ""
    port_type: str = "data"              # "data" | "status" | "control"
    rtovr_src_id: Optional[int] = None   # 对应 RtoVRInputSource 编号

@dataclass
class UnitSpec:
    unit_name: str
    latency: int
    throughput: int
    desc: str
    ports: Dict[str, List[PortSpec]]
    micro_instructions: List[MicroInstructionField]
    behavioral_semantics: Optional[Dict[str, Any]] = None

    @property
    def total_payload_width(self) -> int:
        return sum(f.width for f in self.micro_instructions)

class SpecLoader:
    """
    YAML Spec 加载器 2.0 (SSoT 版)
    负责加载增强型的硬件规格文件，并提供类型安全的数据访问。
    """
    def __init__(self):
        current_dir = os.path.dirname(os.path.abspath(__file__))
        self.base_dir = os.path.join(os.path.dirname(current_dir), "specs")
        self.units_dir = os.path.join(self.base_dir, "units")
        self.system_dir = os.path.join(self.base_dir, "system")
        self._master_data = None
        self._master_loaded = False

    def _load_master(self):
        """延迟加载大一统规格书"""
        if self._master_loaded:
            return
        master_path = os.path.join(self.base_dir, "aiacc_v2.4_master.yaml")
        if os.path.exists(master_path):
            with open(master_path, 'r', encoding='utf-8') as f:
                self._master_data = yaml.safe_load(f)
        self._master_loaded = True

    def get_unit(self, name: str) -> Optional[UnitSpec]:
        """加载并返回特定单元的规格对象 (优先从 Master Spec 加载)"""
        self._load_master()
        
        data = None
        if self._master_data and name.upper() in self._master_data.get("functional_units", {}):
            data = self._master_data["functional_units"][name.upper()]
        else:
            # 回退到独立 YAML
            file_path = os.path.join(self.units_dir, f"{name.lower()}.yaml")
            if os.path.exists(file_path):
                with open(file_path, 'r', encoding='utf-8') as f:
                    data = yaml.safe_load(f)
            
        if not data:
            return None
            
        # 解析端口
        def _parse_port(p: dict) -> PortSpec:
            return PortSpec(
                name=p["name"],
                width=p["width"],
                desc=p.get("desc", ""),
                port_type=p.get("type", "data"),
                rtovr_src_id=p.get("rtovr_src_id")
            )
        ports = {
            "inputs": [_parse_port(p) for p in data.get("ports", {}).get("inputs", [])],
            "outputs": [_parse_port(p) for p in data.get("ports", {}).get("outputs", [])]
        }
        
        # 解析微指令
        micro_instrs = []
        for mi in data.get("micro_instructions", []):
            micro_instrs.append(MicroInstructionField(
                name=mi["field"],
                width=mi["width"],
                default=mi.get("default"),
                encoding=mi.get("encoding")
            ))
            
        return UnitSpec(
            unit_name=data["unit_name"],
            latency=data["latency"],
            throughput=data.get("attributes", {}).get("throughput", 1),
            desc=data.get("attributes", {}).get("desc", ""),
            ports=ports,
            micro_instructions=micro_instrs,
            behavioral_semantics=data.get("behavioral_semantics")
        )

    def get_system_core(self) -> Bunch:
        """从 Master Spec 或 system_core.yaml 加载全局配置"""
        self._load_master()
        
        if self._master_data:
            return Bunch(self._master_data["system_core"])
            
        # 回退逻辑
        file_path = os.path.join(self.system_dir, "system_core.yaml")
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"System core spec not found at {file_path}")
        with open(file_path, 'r', encoding='utf-8') as f:
            data = yaml.safe_load(f)
            return Bunch(data)

if __name__ == "__main__":
    loader = SpecLoader()
    # 演示加载 Unit
    intadd = loader.get_unit("INTADD")
    if intadd:
        print(f"[Unit] Loaded {intadd.unit_name}, Descriptor: {intadd.desc}")
    
    # 演示加载 System Core
    sys_core = loader.get_system_core()
    print(f"[System] Version: {sys_core.system_info.version}")
    print(f"[System] UR Bank Count: {sys_core.global_constants.UR_BANK_COUNT}")
    print(f"[System] Read Ports: {sys_core.resource_constraints.read_ports_per_bank}")
