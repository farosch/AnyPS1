#!/usr/bin/env python3

import argparse
import json
from pathlib import Path


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate the C++ MIPS opcode registry from canonical JSON")
    parser.add_argument("--registry", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    registry = json.loads(args.registry.read_text(encoding="utf-8"))
    opcodes = [None] * 64
    special = [None] * 64
    for entry in registry["opcodes"]:
        opcodes[int(entry["value"])] = entry
    for entry in registry["special"]:
        special[int(entry["value"])] = entry
    if any(entry is None for entry in opcodes):
        raise ValueError("opcode registry must contain all 64 primary opcode values")
    if any(entry is None for entry in special):
        raise ValueError("opcode registry must contain all 64 SPECIAL function values")

    lines = [
        "#pragma once",
        "",
        "#include <array>",
        "#include <cstdint>",
        "",
        "namespace anyps1 {",
        "namespace mips_registry {",
        "",
        "struct SpecialEntry {",
        "    const char* name;",
        "    const char* format;",
        "    bool reserved;",
        "};",
        "",
        "inline const std::array<const char*, 64> opcode_names = {{",
    ]
    lines.append("    " + ", ".join(c_string(entry["name"]) for entry in opcodes))
    lines.extend([
        "}};",
        "",
        "inline const std::array<SpecialEntry, 64> special_entries = {{",
    ])
    for entry in special:
        reserved = "true" if entry["kind"] == "reserved" else "false"
        lines.append(
            "    {" + c_string(entry["name"]) + ", "
            + c_string(entry["format"]) + ", " + reserved + "},")
    lines.extend([
        "}};",
        "",
        "} // namespace mips_registry",
        "} // namespace anyps1",
        "",
    ])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
