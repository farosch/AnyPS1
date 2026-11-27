from pathlib import Path


def required_directories(root: Path) -> tuple[Path, ...]:
    return (
        root / "core" / "relinker",
        root / "tools",
        root / "tests",
        root / "3rdparty",
    )


def required_files(root: Path) -> tuple[Path, ...]:
    return (
        root / "core" / "anyps1.cpp",
    )


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    missing = [str(path) for path in required_directories(root) if not path.is_dir()]
    missing += [str(path) for path in required_files(root) if not path.is_file()]
    if missing:
        raise SystemExit("Missing AnyPS1 layout: " + ", ".join(missing))
