#!/usr/bin/env python3

import argparse
import subprocess
import sys
from pathlib import Path


def boot_executable(system_cnf: Path) -> Path | None:
    for raw_line in system_cnf.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        if "=" not in line or line.split("=", 1)[0].strip().upper() != "BOOT":
            continue
        boot = line.split("=", 1)[1].strip().rstrip()
        for prefix in ("cdrom:\\", "cdrom:/", "cdrom:"):
            if boot.lower().startswith(prefix):
                boot = boot[len(prefix):]
                break
        boot = boot.split(";", 1)[0].strip()
        boot = boot.replace("\\", "/")
        if not boot:
            return None
        direct = system_cnf.parent / boot
        if direct.is_file():
            return direct
        folded = boot.casefold()
        for candidate in system_cnf.parent.iterdir():
            if candidate.is_file() and candidate.name.casefold() == folded:
                return candidate
    return None


def describe_result(binary: Path, folder: Path, timeout: int) -> tuple[str, str]:
    try:
        completed = subprocess.run(
            [str(binary), "--target", "linux", str(folder)],
            cwd=str(folder),
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return "timeout", f"relink exceeded {timeout}s"

    if completed.returncode == 0:
        return "success", "native Linux executable written"

    diagnostic = (completed.stderr or completed.stdout).strip()
    first_line = diagnostic.splitlines()[0] if diagnostic else "unknown relinker failure"
    if first_line.startswith("Error: "):
        first_line = first_line[len("Error: "):]
    return "gap", first_line


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Report native relinking gaps from locally available extracted executables"
    )
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()

    if not args.binary.is_file():
        print(f"relinker binary not found: {args.binary}", file=sys.stderr)
        return 1

    rows = []
    for system_cnf in sorted(args.reference_root.rglob("SYSTEM.CNF")):
        boot = boot_executable(system_cnf)
        if boot is None:
            continue
        status, detail = describe_result(args.binary, system_cnf.parent, args.timeout)
        rows.append({
            "executable": str(boot.relative_to(args.reference_root)),
            "status": status,
            "detail": detail,
        })

    args.output.parent.mkdir(parents=True, exist_ok=True)
    lines = ["AnyPS1 reference relink report", ""]
    for row in rows:
        lines.append(
            f"{row['executable']} | {row['status']} | {row['detail']}"
        )
    if not rows:
        lines.append("No extracted boot executables found.")
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")

    for row in rows:
        print(f"{row['executable']} | {row['status']} | {row['detail']}")
    if not rows:
        print("No extracted boot executables found.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
