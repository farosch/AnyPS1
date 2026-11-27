#!/usr/bin/env python3

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify minimal CFG relinking completes within a timeout")
    parser.add_argument("--relinker", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=10.0)
    args = parser.parse_args()

    if not args.relinker.is_file():
        raise FileNotFoundError(args.relinker)
    if not args.fixture.is_file():
        raise FileNotFoundError(args.fixture)

    with tempfile.TemporaryDirectory(prefix="anyps1-cfg-timeout-") as temp:
        folder = Path(temp)
        (folder / "SCUS_941.63").write_bytes(args.fixture.read_bytes())
        (folder / "SYSTEM.CNF").write_text(
            "BOOT=cdrom:\\SCUS_941.63;1\n",
            encoding="utf-8",
        )
        try:
            target = "windows" if os.name == "nt" else "linux"
            artifact = "game.exe" if target == "windows" else "game.elf"
            completed = subprocess.run(
                [str(args.relinker), "--target", target, str(folder)],
                capture_output=True,
                check=False,
                timeout=args.timeout,
            )
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(
                f"CFG relinking exceeded {args.timeout:.1f}s"
            ) from error
        if completed.returncode != 0:
            raise RuntimeError(
                completed.stderr.decode("utf-8", errors="replace").strip()
                or "CFG relinking failed")
        if not (folder / artifact).is_file():
            raise RuntimeError(f"CFG relinking did not retain {artifact}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
