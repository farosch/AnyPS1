import html
import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = ROOT / "src"
TEST_ROOT = ROOT / "tests"
FEATURE_FILE = ROOT / "tools" / "progress" / "features.json"
OUT_ROOT = ROOT / "preview"

COLORS = {
    "tested": "#3fb950",
    "implemented": "#58a6ff",
    "partial": "#d29922",
    "unsupported": "#f85149",
    "missing": "#484f58",
    "not_applicable": "#6e7681",
}


def source_files():
    return sorted(
        path for path in SOURCE_ROOT.rglob("*")
        if path.is_file() and path.suffix in {".cpp", ".h", ".inc"}
    )


def test_files():
    return sorted(
        path for path in TEST_ROOT.rglob("*")
        if path.is_file() and (
            path.suffix in {".cpp", ".h", ".py"}
            or path.name == "CMakeLists.txt"
        )
    )


def read_text(path):
    return path.read_text(encoding="utf-8", errors="replace")


def find_matches(path_text, patterns):
    matches = []
    for pattern in patterns:
        try:
            regex = re.compile(pattern)
            matches.extend(regex.finditer(path_text))
        except re.error:
            if pattern in path_text:
                matches.append(re.search(re.escape(pattern), path_text))
    return [match for match in matches if match is not None]


def first_location(relative_path, pattern):
    path = ROOT / relative_path
    if not path.is_file():
        return None
    text = read_text(path)
    match = find_matches(text, [pattern])
    if not match:
        return None
    line = text.count("\n", 0, match[0].start()) + 1
    return {"path": relative_path, "line": line}


def source_evidence(feature):
    evidence = []
    matched_source_patterns = 0
    source_patterns = feature.get("source_patterns", [])
    for source_ref in source_patterns:
        relative_path = source_ref["path"]
        patterns = source_ref.get("patterns", [])
        path = ROOT / relative_path
        if not path.is_file():
            continue
        text = read_text(path)
        matched = find_matches(text, patterns)
        if matched:
            matched_source_patterns += 1
            first = matched[0]
            line = text.count("\n", 0, first.start()) + 1
            evidence.append({
                "path": relative_path,
                "line": line,
                "pattern": patterns[0],
            })
    return evidence, matched_source_patterns


def test_evidence(feature):
    evidence = []
    for test_ref in feature.get("test_patterns", []):
        relative_path = test_ref["path"]
        patterns = test_ref.get("patterns", [])
        path = ROOT / relative_path
        if not path.is_file():
            continue
        text = read_text(path)
        matched = find_matches(text, patterns)
        if matched:
            first = matched[0]
            line = text.count("\n", 0, first.start()) + 1
            evidence.append({
                "path": relative_path,
                "line": line,
                "pattern": patterns[0],
            })
    return evidence


def feature_status(feature):
    if feature.get("not_applicable"):
        return "not_applicable"

    evidence, matched = source_evidence(feature)
    unsupported = test_evidence(feature)
    unsupported_evidence = []
    for unsupported_ref in feature.get("unsupported_patterns", []):
        path = ROOT / unsupported_ref["path"]
        if not path.is_file():
            continue
        text = read_text(path)
        matched_unsupported = find_matches(text, unsupported_ref.get("patterns", []))
        if matched_unsupported:
            first = matched_unsupported[0]
            line = text.count("\n", 0, first.start()) + 1
            unsupported_evidence.append({
                "path": unsupported_ref["path"],
                "line": line,
                "pattern": unsupported_ref["patterns"][0],
            })
    if unsupported_evidence:
        return "unsupported"
    if not evidence:
        return "missing"

    source_count = len(feature.get("source_patterns", []))
    if source_count > 1 and matched < source_count:
        return "partial"
    if test_evidence(feature):
        return "tested"
    if feature.get("partial"):
        return "partial"
    return "implemented"


def git_commit():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=ROOT,
            text=True,
        ).strip()
    except subprocess.SubprocessError:
        return "unknown"


def ctest_names():
    cmake_file = TEST_ROOT / "CMakeLists.txt"
    if not cmake_file.is_file():
        return []
    text = read_text(cmake_file)
    names = []
    for match in re.finditer(r"add_test\s*\(\s*NAME\s+([A-Za-z0-9_.+-]+)", text):
        names.append(match.group(1))
    return names


def source_url(ref):
    path = ref["path"].replace("\\", "/")
    line = ref.get("line", 1)
    return f"../{path}#L{line}"


def rasterize_svg(svg_path, png_path):
    command = [
        "ffmpeg", "-y", "-i", str(svg_path),
        "-frames:v", "1", str(png_path),
    ]
    try:
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return png_path.exists()
    except (FileNotFoundError, subprocess.CalledProcessError):
        return False


def main():
    feature_data = json.loads(FEATURE_FILE.read_text(encoding="utf-8"))
    records = []

    for subsystem in feature_data["subsystems"]:
        for feature in subsystem["items"]:
            source_evidence_items, _ = source_evidence(feature)
            test_evidence_items = test_evidence(feature)
            unsupported_evidence = []
            for unsupported_ref in feature.get("unsupported_patterns", []):
                path = ROOT / unsupported_ref["path"]
                if not path.is_file():
                    continue
                text = read_text(path)
                matches = find_matches(text, unsupported_ref.get("patterns", []))
                if matches:
                    first = matches[0]
                    unsupported_evidence.append({
                        "path": unsupported_ref["path"],
                        "line": text.count("\n", 0, first.start()) + 1,
                        "pattern": unsupported_ref["patterns"][0],
                    })
            status = feature_status(feature)
            records.append({
                "subsystem": subsystem["name"],
                "name": feature["name"],
                "status": status,
                "source": source_evidence_items,
                "tests": test_evidence_items,
                "unsupported": unsupported_evidence,
            })

    OUT_ROOT.mkdir(parents=True, exist_ok=True)
    grouped = {}
    for record in records:
        grouped.setdefault(record["subsystem"], []).append(record)

    cell_w = 72
    cell_h = 56
    left = 210
    top = 82
    width = left + max(len(items) for items in grouped.values()) * cell_w + 50
    height = top + len(grouped) * cell_h + 96

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" role="img" aria-label="AnyPS1 implementation raster">',
        '<rect width="100%" height="100%" fill="#0d1117"/>',
        '<style>',
        '.label{fill:#c9d1d9;font-family:sans-serif;font-size:12px}',
        '.title{fill:#f0f6fc;font-family:sans-serif;font-size:18px;font-weight:bold}',
        '.legend{fill:#c9d1d9;font-family:sans-serif;font-size:11px}',
        '</style>',
        '<text x="20" y="34" class="title">AnyPS1 implementation raster</text>',
        '<text x="20" y="56" class="label">Source-derived implementation and test coverage</text>',
    ]
    for row, (subsystem, items) in enumerate(grouped.items()):
        y = top + row * cell_h
        svg.append(f'<text x="20" y="{y + cell_h / 2}" dominant-baseline="middle" class="label">{html.escape(subsystem)}</text>')
        for column, record in enumerate(items):
            x = left + column * cell_w
            status = record["status"]
            title = f"{subsystem}: {record['name']} - {status}"
            if record["source"]:
                title += " | source " + ", ".join(f"{ref['path']}:{ref['line']}" for ref in record["source"])
            if record["tests"]:
                title += " | tests " + ", ".join(f"{ref['path']}:{ref['line']}" for ref in record["tests"])
            href = None
            if record["source"]:
                href = source_url(record["source"][0])
            svg.append(
                f'<a href="{html.escape(href or "../README.md", quote=True)}">'
                f'<rect x="{x}" y="{y}" width="{cell_w-5}" height="{cell_h-5}" rx="5" fill="{COLORS[status]}">'
                f'<title>{html.escape(title)}</title></rect>'
                f'<text x="{x + (cell_w-5)/2}" y="{y + (cell_h-5)/2}" text-anchor="middle" dominant-baseline="middle" class="label" font-size="10">'
                f'{html.escape(record["name"][:6])}</text></a>'
            )
    legend_y = top + len(grouped) * cell_h + 28
    legend_x = 20
    svg.append(f'<text x="20" y="{legend_y-18}" class="label">Status legend</text>')
    for status in ("tested", "implemented", "partial", "unsupported", "missing", "not_applicable"):
        svg.append(
            f'<rect x="{legend_x}" y="{legend_y}" width="14" height="14" rx="3" fill="{COLORS[status]}"/>'
            f'<text x="{legend_x + 18}" y="{legend_y + 12}" class="legend">{html.escape(status.replace("_", " "))}</text>'
        )
        legend_x += 118
    svg.append("</svg>")

    svg_path = OUT_ROOT / "progress.svg"
    png_path = OUT_ROOT / "progress.png"
    svg_path.write_text("\n".join(svg), encoding="utf-8")
    rasterized = rasterize_svg(svg_path, png_path)

    counts = {}
    totals = {"tested": 0, "implemented": 0, "partial": 0, "unsupported": 0, "missing": 0, "not_applicable": 0}
    for subsystem, items in grouped.items():
        states = {}
        for status in totals:
            states[status] = sum(1 for item in items if item["status"] == status)
            totals[status] += states[status]
        counts[subsystem] = states

    summary = {
        "commit": git_commit(),
        "total": len(records),
        "status_counts": totals,
        "subsystems": counts,
        "rasterized_png": rasterized,
        "ctest_names": ctest_names(),
        "implemented_percent": round(
            100 * (totals["tested"] + totals["implemented"] + totals["partial"])
            / max(1, len(records) - totals["not_applicable"]),
            1,
        ),
        "tested_percent": round(
            100 * totals["tested"] / max(1, len(records) - totals["not_applicable"]),
            1,
        ),
        "stale_test_coverage": sum(
            1 for record in records
            if record["tests"] and not record["source"] and record["status"] == "missing"
        ),
    }
    (OUT_ROOT / "progress.json").write_text(
        json.dumps({
            "summary": summary,
            "features": records,
        }, indent=2) + "\n",
        encoding="utf-8",
    )

    cards = []
    for status in ("tested", "implemented", "partial", "unsupported", "missing", "not_applicable"):
        cards.append(
            f'<div class="card"><span class="dot {status}"></span>'
            f'<strong>{totals[status]}</strong><span>{html.escape(status.replace("_", " "))}</span></div>'
        )
    cards.append(
        f'<div class="card"><strong>{summary["implemented_percent"]}%</strong><span>implemented</span></div>'
    )
    cards.append(
        f'<div class="card"><strong>{summary["tested_percent"]}%</strong><span>tested</span></div>'
    )
    cards.append(
        f'<div class="card"><strong>{summary["stale_test_coverage"]}</strong><span>stale test coverage</span></div>'
    )

    filters = []
    for status in ("tested", "implemented", "partial", "unsupported", "missing", "not_applicable"):
        filters.append(
            f'<button class="filter" data-status="{status}">{html.escape(status.replace("_", " "))}</button>'
        )

    table_rows = []
    for record in records:
        source_links = "".join(
            f'<a href="{source_url(ref)}">{html.escape(ref["path"])}:{ref["line"]}</a><br>'
            for ref in record["source"]
        )
        test_links = "".join(
            f'<a href="{source_url(ref)}">{html.escape(ref["path"])}:{ref["line"]}</a><br>'
            for ref in record["tests"]
        )
        unsupported_links = "".join(
            f'<a href="{source_url(ref)}">{html.escape(ref["path"])}:{ref["line"]}</a><br>'
            for ref in record["unsupported"]
        )
        table_rows.append(
            f'<tr data-status="{record["status"]}">'
            f'<td>{html.escape(record["subsystem"])}</td>'
            f'<td>{html.escape(record["name"])}</td>'
            f'<td><span class="status {record["status"]}">{record["status"].replace("_", " ")}</span></td>'
            f'<td>{source_links or "-"}</td>'
            f'<td>{test_links or "-"}</td>'
            f'<td>{unsupported_links or "-"}</td>'
            f'</tr>'
        )

    html_doc = f'''<!doctype html>
<html><head><meta charset="utf-8"><title>AnyPS1 implementation progress</title>
<style>
:root {{ color-scheme: dark; }}
body {{ background:#0d1117; color:#c9d1d9; font-family:sans-serif; margin:24px; }}
a {{ color:#58a6ff; }}
.summary {{ display:flex; flex-wrap:wrap; gap:10px; margin:14px 0 20px; }}
.card {{ border:1px solid #30363d; border-radius:8px; padding:10px 14px; min-width:110px; display:flex; gap:8px; align-items:center; }}
.card strong {{ font-size:20px; }}
.dot {{ width:10px; height:10px; border-radius:50%; display:inline-block; }}
.dot.tested,.status.tested {{ background:#3fb950; }} .status.tested {{ background:transparent; color:#3fb950; }}
.dot.implemented,.status.implemented {{ background:#58a6ff; }} .status.implemented {{ background:transparent; color:#58a6ff; }}
.dot.partial,.status.partial {{ background:#d29922; }} .status.partial {{ background:transparent; color:#d29922; }}
.dot.unsupported,.status.unsupported {{ background:#f85149; }} .status.unsupported {{ background:transparent; color:#f85149; }}
.dot.missing,.status.missing {{ background:#484f58; }} .status.missing {{ background:transparent; color:#8b949e; }}
.dot.not_applicable,.status.not_applicable {{ background:#6e7681; }} .status.not_applicable {{ background:transparent; color:#6e7681; }}
.filters {{ display:flex; gap:8px; flex-wrap:wrap; margin-bottom:12px; }}
.filter {{ background:#21262d; color:#c9d1d9; border:1px solid #30363d; border-radius:6px; padding:6px 10px; cursor:pointer; }}
.filter.active {{ border-color:#58a6ff; }}
.raster {{ max-width:100%; border:1px solid #30363d; margin-bottom:16px; }}
table {{ border-collapse:collapse; width:100%; }} th,td {{ border:1px solid #30363d; padding:7px 9px; vertical-align:top; }} th {{ position:sticky; top:0; background:#161b22; }}
tr.hidden {{ display:none; }}
.status {{ font-weight:bold; }}
</style></head><body>
<h1>AnyPS1 implementation progress</h1>
<p>Commit <code>{html.escape(summary['commit'])}</code>: source-derived implementation and test coverage.</p>
<div class="summary">{''.join(cards)}</div>
<div class="filters"><button class="filter active" data-status="all">all</button>{''.join(filters)}</div>
<img class="raster" src="progress.svg" alt="AnyPS1 implementation raster">
<table><thead><tr><th>Subsystem</th><th>Feature</th><th>Status</th><th>Source evidence</th><th>Test evidence</th><th>Unsupported evidence</th></tr></thead>
<tbody>{''.join(table_rows)}</tbody></table>
<script>
const buttons=[...document.querySelectorAll('.filter')];
buttons.forEach(button=>button.addEventListener('click',()=>{{
  buttons.forEach(item=>item.classList.remove('active'));
  button.classList.add('active');
  const status=button.dataset.status;
  document.querySelectorAll('tbody tr').forEach(row=>{{
    row.classList.toggle('hidden', status !== 'all' && row.dataset.status !== status);
  }});
}}));
</script>
</body></html>'''
    (OUT_ROOT / "progress.html").write_text(html_doc, encoding="utf-8")
    print(OUT_ROOT / "progress.html")


if __name__ == "__main__":
    main()
