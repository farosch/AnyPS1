from pathlib import Path


def required_directories(root: Path) -> tuple[Path, ...]:
    return (
        root / "src",
        root / "src" / "relinker",
        root / "src" / "format",
        root / "src" / "lowering",
        root / "src" / "executable",
        root / "tools",
        root / "tests",
        root / "3rdparty",
    )


def required_files(root: Path) -> tuple[Path, ...]:
    return (
        root / "src" / "CMakeLists.txt",
        root / "src" / "main.cpp",
        root / "src" / "relinker" / "Relinker.h",
        root / "src" / "relinker" / "Relinker.cpp",
    )


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    missing = [str(path) for path in required_directories(root) if not path.is_dir()]
    missing += [str(path) for path in required_files(root) if not path.is_file()]
    if missing:
        raise SystemExit("Missing AnyPS1 layout: " + ", ".join(missing))
