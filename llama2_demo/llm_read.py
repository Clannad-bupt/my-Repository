from safetensors import safe_open
import json
import os


class LlmRead:
    def __init__(self, llm_path: str):
        self.sf_files = []
        self.llm_path = llm_path
        self.key_files = {}
        for entry in os.listdir(llm_path):
            full_path = os.path.join(llm_path, entry)
            if os.path.isfile(full_path) and entry.endswith(".safetensors"):
                f = safe_open(full_path, framework="numpy")
                self.sf_files.append(f)
                for key in f.keys():
                    self.key_files[key] = f
        # HF config.json (rope_theta 等); 缺失时为空 dict, 由调用方走默认值
        self.config = {}
        cfg_path = os.path.join(llm_path, "config.json")
        if os.path.isfile(cfg_path):
            with open(cfg_path) as f:
                self.config = json.load(f)

    def read(self, key: str):
        if key not in self.key_files:
            raise KeyError(f"{key} not found in any safetensors file")
        f = self.key_files[key]
        return f.get_tensor(key)
