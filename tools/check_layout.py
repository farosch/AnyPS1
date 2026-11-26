from pathlib import Path


def required_layout(root: Path) -> tuple[Path, ...]:
    return (
        root / "core" / "relinker",
        root / "core" / "libs",
        root / "tools",
        root / "cmake",
        root / "docs",
        root / "demo",
        root / "tests",
        root / "3rdparty",
    )


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    missing = [str(path) for path in required_layout(root) if not path.is_dir()]
    if missing:
        raise SystemExit("Missing AnyPS1 layout: " + ", ".join(missing))
