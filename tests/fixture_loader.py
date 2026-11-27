import argparse
import base64
import json
import shutil
import struct
from pathlib import Path


PSX_HEADER_SIZE = 2048


def build_psx_exe(description: dict) -> bytes:
    kind = description.get("kind")
    if kind == "raw":
        return base64.b64decode(description["base64"])
    if kind != "psx_exe":
        raise ValueError(f"unsupported executable fixture kind: {kind}")

    header = description.get("header", {})
    instructions = description.get("instructions", [])
    file_size = int(header.get("file_size", len(instructions) * 4))
    data = bytearray(max(PSX_HEADER_SIZE + file_size, PSX_HEADER_SIZE))
    data[:8] = b"PS-X EXE"
    fields = {
        0x10: "initial_pc",
        0x14: "initial_gp",
        0x18: "load_address",
        0x1C: "file_size",
        0x30: "initial_sp_base",
        0x34: "initial_sp_offset",
    }
    for offset, name in fields.items():
        struct.pack_into("<I", data, offset, int(header.get(name, 0)))
    for index, word in enumerate(instructions):
        offset = PSX_HEADER_SIZE + index * 4
        if offset + 4 <= len(data):
            struct.pack_into("<I", data, offset, int(word))
    return bytes(data)


def materialize(source: Path, output: Path) -> None:
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    for description_path in sorted(source.rglob("*.json")):
        rel = description_path.relative_to(source)
        description = json.loads(description_path.read_text(encoding="utf-8"))
        kind = description.get("kind")
        target = output / rel

        if kind == "target_parity":
            target = target.with_suffix(".json")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(
                json.dumps(description, indent=2) + "\n",
                encoding="utf-8",
            )
        elif kind in ("psx_exe", "raw"):
            target = target.with_suffix(".ps-exe")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(build_psx_exe(description))
        elif kind == "words":
            target = target.with_suffix(".bin")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"".join(struct.pack("<I", int(word)) for word in description["words"]))

    (output / "fixture-manifest.json").write_text(
        json.dumps({"format": "AnyPS1 generated test fixtures"}, indent=2) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Materialize JSON fixtures into test binaries")
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    materialize(args.source.resolve(), args.output.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
