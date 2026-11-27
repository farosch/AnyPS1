#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
from pathlib import Path


def inventory(executable: Path, output: Path) -> dict:
    subprocess.run(
        [
            sys.executable,
            str(Path(__file__).with_name("inventory_unsupported_instructions.py")),
            str(executable),
            "--output",
            str(output),
        ],
        check=True,
    )
    reports = json.loads(output.read_text(encoding="utf-8"))
    if not isinstance(reports, list) or len(reports) != 1:
        raise RuntimeError("reference inventory produced an invalid report")
    return reports[0]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate real boot executable unsupported-instruction inventories"
    )
    parser.add_argument("executables", nargs="+", type=Path)
    parser.add_argument("--output-dir", type=Path, default=Path("build/reference-inventories"))
    parser.add_argument(
        "--allow-host-unsupported",
        action="store_true",
        help="accept known host operations while still rejecting instruction families",
    )
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    failed = False
    for executable in args.executables:
        if not executable.is_file():
            print(f"{executable}: missing", file=sys.stderr)
            failed = True
            continue
        output = args.output_dir / (executable.stem + "-inventory.json")
        report = inventory(executable, output)
        unsupported = report.get("unsupported_count", 0)
        families = report.get("unsupported_families", {})
        print(
            f"{executable}: unsupported_instructions={unsupported} "
            f"families={families}"
        )
        if unsupported:
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
