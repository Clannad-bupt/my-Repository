#!/usr/bin/env python3
"""Generate Vivado Tcl that recreates IP from newer JSON XCI user settings."""

import argparse
import json
from pathlib import Path


def tcl_quote(value: object) -> str:
    text = str(value)
    return "{" + text.replace("\\", "\\\\").replace("}", "\\}") + "}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--output-tcl", required=True)
    args = parser.parse_args()

    source_dir = Path(args.source_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_tcl = Path(args.output_tcl).resolve()
    xci_files = sorted(source_dir.glob("*/*.xci"))
    if not xci_files:
        raise SystemExit(f"No XCI files found below {source_dir}")

    lines = [
        "# Auto-generated: recreate IP using the running Vivado catalog.",
        f"file mkdir {tcl_quote(output_dir)}",
    ]
    for xci in xci_files:
        doc = json.loads(xci.read_text(encoding="utf-8-sig"))
        inst = doc["ip_inst"]
        module_name = inst["xci_name"]
        vendor, library, ip_name, ip_version = inst["component_reference"].split(":")
        ip_dir = output_dir / module_name
        lines.extend(
            [
                f"file mkdir {tcl_quote(ip_dir)}",
                "create_ip "
                f"-name {tcl_quote(ip_name)} -vendor {tcl_quote(vendor)} "
                f"-library {tcl_quote(library)} -version {tcl_quote(ip_version)} "
                f"-module_name {tcl_quote(module_name)} -dir {tcl_quote(ip_dir)}",
                f"set recreated_ip [get_ips {tcl_quote(module_name)}]",
            ]
        )
        parameters = inst["parameters"]["component_parameters"]
        for key, entries in parameters.items():
            if not entries:
                continue
            entry = entries[0]
            # Only migrate settings explicitly changed by the user. Newer XCI
            # files also label many expanded/default values as resolve_type=user;
            # replaying those prevents the older IP from computing its own
            # version-correct derived values (especially for HBM and clk_wiz).
            if entry.get("value_src") != "user" or key == "Component_Name":
                continue
            value = entry.get("value", "")
            message = f"Vivado 2021.x does not accept CONFIG.{key}={value} for {module_name}"
            lines.append(
                f"if {{[catch {{set_property CONFIG.{key} {tcl_quote(value)} $recreated_ip}} cfg_err]}} "
                f"{{puts {tcl_quote('WARNING: ' + message + ': $cfg_err')}}}"
            )
        lines.append("generate_target all $recreated_ip")

    output_tcl.parent.mkdir(parents=True, exist_ok=True)
    output_tcl.write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
