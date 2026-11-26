import argparse
import json
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import inspect_psx_disc


PAYLOAD_SIZE = 2048


@dataclass(frozen=True)
class CueTrack:
    number: int
    mode: str
    bin_file: str
    index_offset: int = 0
    start_lba: int = 0
    file_start_lba: int = 0


def parse_cue(cue_path: Path) -> list[CueTrack]:
    tracks = []
    current_file = None
    file_start_lba = 0
    pending_track = None
    pending_index_offset = 0
    cue_directory = cue_path.parent
    for raw_line in cue_path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        file_match = re.match(r'FILE\s+"([^"]+)"\s+\S+', line, re.IGNORECASE)
        if file_match:
            bin_name = file_match.group(1)
            bin_path = cue_directory / bin_name
            if not bin_path.is_file():
                raise ValueError(f"CUE track file not found: {bin_path}")
            current_file = bin_name
            if current_file and tracks:
                previous = tracks[-1]
                previous_size = (cue_directory / previous.bin_file).stat().st_size
                previous_sectors = previous_size // 2352
                file_start_lba += previous_sectors
            continue
        track_match = re.match(r"TRACK\s+(\d+)\s+(\S+)", line, re.IGNORECASE)
        if track_match and current_file:
            pending_track = CueTrack(
                int(track_match.group(1)),
                track_match.group(2).upper(),
                current_file,
                0,
                file_start_lba,
                file_start_lba,
            )
            tracks.append(pending_track)
            continue
        index_match = re.match(r"INDEX\s+(\d+)\s+(\d{2}):(\d{2}):(\d{2})", line, re.IGNORECASE)
        if index_match and pending_track:
            index_number = int(index_match.group(1))
            if index_number in (0, 1):
                offset = int(index_match.group(2)) * 60 * 75
                offset += int(index_match.group(3)) * 75
                offset += int(index_match.group(4))
                if index_number == 1:
                    tracks[-1] = CueTrack(
                        pending_track.number,
                        pending_track.mode,
                        pending_track.bin_file,
                        offset,
                        file_start_lba + offset,
                        file_start_lba,
                    )
                    pending_track = None
    if not tracks:
        raise ValueError("CUE sheet contains no tracks")
    return tracks


def find_data_track(cue_path: Path) -> tuple[CueTrack, Path]:
    tracks = parse_cue(cue_path)
    for track in tracks:
        if track.mode in ("MODE1/2352", "MODE2/2352"):
            bin_path = cue_path.parent / track.bin_file
            return track, bin_path
    raise ValueError("CUE sheet contains no MODE1/2352 or MODE2/2352 data track")


def convert_data_track(track: CueTrack, bin_path: Path, disc_iso: Path) -> None:
    payload_offset = 24 if track.mode == "MODE2/2352" else 16
    sector_size = 2352
    with bin_path.open("rb") as source, disc_iso.open("wb") as output:
        while True:
            sector = source.read(sector_size)
            if not sector:
                break
            if len(sector) != sector_size:
                raise ValueError(f"truncated 2352-byte sector in {bin_path}")
            payload = sector[payload_offset:payload_offset + PAYLOAD_SIZE]
            if len(payload) != PAYLOAD_SIZE:
                raise ValueError(f"invalid payload sector in {bin_path}")
            output.write(payload)


def safe_output_path(root: Path, entry_path: str) -> Path:
    normalized = entry_path.replace("\\", "/").strip("/")
    parts = normalized.split("/")
    if not parts or any(part in ("", ".", "..") for part in parts):
        raise ValueError(f"unsafe ISO entry path: {entry_path}")
    destination = root.joinpath(*parts)
    if root not in destination.resolve().parents:
        raise ValueError(f"ISO entry escapes extraction directory: {entry_path}")
    return destination


def read_external_entry(
    cue_path: Path,
    tracks: list[CueTrack],
    entry_lba: int,
    entry_size: int,
) -> bytes:
    output = bytearray()
    remaining = entry_size
    for track in tracks:
        if not track.mode.startswith("AUDIO"):
            continue
        bin_path = cue_path.parent / track.bin_file
        track_sectors = bin_path.stat().st_size // 2352
        track_start = track.file_start_lba
        track_end = track_start + track_sectors
        if track_start <= entry_lba < track_end:
            sector_offset = entry_lba - track_start
            with bin_path.open("rb") as stream:
                stream.seek(sector_offset * 2352)
                readable = min(remaining, (track_end - entry_lba) * 2352)
                while readable:
                    chunk = stream.read(min(64 * 1024, readable))
                    if not chunk:
                        break
                    output.extend(chunk)
                    remaining -= len(chunk)
                    readable -= len(chunk)
            if remaining == 0:
                break
    if remaining:
        raise ValueError(
            f"external track entry at LBA {entry_lba} exceeds available CUE tracks"
        )
    return bytes(output)


def extract_game(cue_path: Path, output: Path) -> dict:
    cue_path = cue_path.resolve()
    output = output.resolve()
    if not cue_path.is_file():
        raise ValueError(f"CUE sheet not found: {cue_path}")

    track, bin_path = find_data_track(cue_path)
    output.mkdir(parents=True, exist_ok=True)
    filesystem_output = output / "filesystem"
    boot_output = output / "boot"
    filesystem_output.mkdir(parents=True, exist_ok=True)
    boot_output.mkdir(parents=True, exist_ok=True)

    cue_tracks = parse_cue(cue_path)
    track_manifest = []
    for cue_track in cue_tracks:
        track_manifest.append(
            {
                "number": cue_track.number,
                "mode": cue_track.mode,
                "bin": cue_track.bin_file,
                "index_offset": cue_track.index_offset,
                "start_lba": cue_track.start_lba,
                "file_start_lba": cue_track.file_start_lba,
            }
        )

    temporary_directory = tempfile.TemporaryDirectory(prefix="anyps1-")
    disc_iso = Path(temporary_directory.name) / "disc.iso"
    convert_data_track(track, bin_path, disc_iso)

    volume_id, entries = inspect_psx_disc.inspect_disc(disc_iso)
    disc_sector_count = disc_iso.stat().st_size // PAYLOAD_SIZE
    extracted_files = 0
    for entry in entries:
        if entry.is_directory:
            continue
        destination = safe_output_path(filesystem_output, entry.path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if entry.lba < disc_sector_count:
            data = inspect_psx_disc.read_entry(disc_iso, entry)
        else:
            data = read_external_entry(
                cue_path,
                cue_tracks,
                entry.lba,
                entry.size,
            )
        destination.write_bytes(data)
        extracted_files += 1

    boot_entry, boot_exe = inspect_psx_disc.find_boot_entry(disc_iso)
    boot_name = boot_entry.path.replace("\\", "/").rstrip("/").split("/")[-1]
    boot_path = boot_output / boot_name
    boot_path.write_bytes(boot_exe)

    system_entry = next(
        entry for entry in entries
        if entry.path.upper().endswith("SYSTEM.CNF")
    )
    system_cnf = inspect_psx_disc.read_entry(disc_iso, system_entry)
    system_path = output / "SYSTEM.CNF"
    system_path.write_bytes(system_cnf)

    header = inspect_psx_disc.parse_psx_exe_header(boot_exe)
    manifest = {
        "format": "AnyPS1 game extraction",
        "volume_id": volume_id,
        "cue": str(cue_path),
        "data_track": {
            "number": track.number,
            "mode": track.mode,
            "bin": str(bin_path.resolve()),
        },
        "filesystem": str(filesystem_output),
        "track_count": len(cue_tracks),
        "track_manifest": track_manifest,
        "system_cnf": str(system_path),
        "boot_entry": boot_entry.path,
        "boot_executable": str(boot_path),
        "boot_header": header,
        "extracted_files": extracted_files,
    }
    manifest_path = output / "game.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract a PS1 CUE/BIN game into the AnyPS1 reference layout"
    )
    parser.add_argument("cue", nargs="+", type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        help="output directory (default: <CUE directory>/extracted)",
    )
    args = parser.parse_args()

    try:
        cue_paths = [cue.resolve() for cue in args.cue]
        output = args.output if args.output else cue_paths[0].parent / "extracted"
        manifests = []
        for index, cue_path in enumerate(cue_paths, start=1):
            disc_output = output / f"disc{index}"
            manifests.append(extract_game(cue_path, disc_output))
        (output / "game.json").write_text(
            json.dumps(
                {
                    "format": "AnyPS1 extraction",
                    "disc_count": len(cue_paths),
                    "discs": [
                        {
                            "number": index,
                            "volume_id": manifest["volume_id"],
                            "cue": manifest["cue"],
                            "filesystem": manifest["filesystem"],
                            "system_cnf": manifest["system_cnf"],
                            "boot_entry": manifest["boot_entry"],
                            "boot_executable": manifest["boot_executable"],
                            "boot_header": manifest["boot_header"],
                            "extracted_files": manifest["extracted_files"],
                        }
                        for index, manifest in enumerate(manifests, start=1)
                    ],
                },
                indent=2,
            ) + "\n",
            encoding="utf-8",
        )
        for manifest in manifests:
            print(f"Volume: {manifest['volume_id']}")
            print(f"Boot executable: {manifest['boot_entry']}")
            print(f"Extracted files: {manifest['extracted_files']}")
        print(f"Output: {output}")
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
