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
    YAML Spec 加载器 2.0
    负责加载增强型的硬件规格文件，并提供类型安全的数据访问。
    """
    def __init__(self):
        current_dir = os.path.dirname(os.path.abspath(__file__))
        specs_ptr = os.path.join(os.path.dirname(current_dir), "specs")
        
        # 🛸 [V5.4 FIX] Handle "redirect file" for specs directory (common on Windows dev environments)
        if os.path.isfile(specs_ptr):
            with open(specs_ptr, "r") as f:
                rel_path = f.read().strip()
                self.base_dir = os.path.abspath(os.path.join(os.path.dirname(specs_ptr), rel_path))
        else:
            self.base_dir = specs_ptr
            
        self.units_dir = os.path.join(self.base_dir, "units")
        self.system_dir = os.path.join(self.base_dir, "system")

    def get_unit(self, name: str) -> Optional[UnitSpec]:
        """加载并返回特定单元的规格对象"""
        file_path = os.path.join(self.units_dir, f"{name.lower()}.yaml")
        if not os.path.exists(file_path):
            return None
            
        with open(file_path, 'r', encoding='utf-8') as f:
            data = yaml.safe_load(f)
            
        # 解析端口（显式字段提取，兼容新旧 Spec）
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
        """从 system_core.yaml 加载全局常量和资源限制"""
        file_path = os.path.join(self.system_dir, "system_core.yaml")
        if not os.path.exists(file_path):
            raise FileNotFoundError(f"System core spec not found at {file_path}")
        with open(file_path, 'r', encoding='utf-8') as f:
            data = yaml.safe_load(f)
            # print(f"DEBUG_SPECLOADER_LOADED_SYSTEM_CORE: {data.get('resource_constraints')}")
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
