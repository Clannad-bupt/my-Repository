from api.hw_spec import HardwareSpec

spec = HardwareSpec.load("hw_config")
dvr_map = spec.get_dvr_map()

print("DVR MAP CONTENTS:")
for unit, ports in dvr_map.items():
    print(f"  Unit: {unit} -> {ports}")

# 模拟查找 UR_WRITE
target_unit = "UR_WRITE"
target_port = "D"
slot = dvr_map.get(target_unit, {}).get(target_port)
print(f"\nLookup UR_WRITE.D -> Slot: {slot}")
