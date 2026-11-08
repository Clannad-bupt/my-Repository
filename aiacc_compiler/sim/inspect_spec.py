import sys, os
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'src'))

from api.hw_spec import HardwareSpec

def inspect_spec():
    spec = HardwareSpec.load("hw_config")
    print("\n" + "="*20 + " HARDWARE SPEC AUDIT " + "="*20)
    
    # 1. 检查 UR_READ 的 Body 位宽
    for iq_key, iq_conf in spec.iqs.items():
        if "UR_READ" in iq_key or "UR-R" in iq_key.replace("_", "-"):
            print(f"IQ: {iq_key}")
            print(f"  - BITS_CODE: {iq_conf.BITS_CODE}")
            print(f"  - EN_LOOPS:  {iq_conf.EN_LOOPS}")
    
    # 2. 检查 Header 定义
    header_fields = spec.config.get("iq_header", [])
    print(f"\nISA_HEADER_BITS: {spec.params.ISA_HEADER_BITS}")
    curr = 0
    for f in header_fields:
        print(f"  - Field: {f['field']:<12} | Width: {f['width']:2} | Start Bit: {curr}")
        curr += f['width']

    # 3. 检查 UR_READ/UR_WRITE 的模块字段定义
    for mname in ["UR_READ", "UR_WRITE"]:
        if mname in spec.modules:
            mod = spec.modules[mname]
            print(f"\nModule: {mname}")
            print(f"  - Total Code Bits: {mod.code_bits}")
            for f in mod.code_fields:
                print(f"    - {f.name:<10}: {f.bits} bits")

if __name__ == "__main__":
    inspect_spec()
