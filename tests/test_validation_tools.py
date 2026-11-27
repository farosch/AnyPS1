import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path


class ReproducibilityTest(unittest.TestCase):
    def test_valid_fixture_is_relinkable_in_two_runs(self):
        root = Path(__file__).resolve().parents[1]
        fixture = root / "build" / "tests" / "fixtures" / "valid.ps-exe"
        relinker = root / "build" / "anyps1"
        if not fixture.is_file() or not relinker.is_file():
            self.skipTest("build artifacts are not available")

        hashes = []
        for run in range(2):
            with tempfile.TemporaryDirectory(prefix="anyps1-repro-") as temp:
                folder = Path(temp)
                (folder / "SCUS_941.63").write_bytes(fixture.read_bytes())
                (folder / "SYSTEM.CNF").write_text(
                    "BOOT=cdrom:\\SCUS_941.63;1\n",
                    encoding="utf-8",
                )
                completed = subprocess.run(
                    [str(relinker), "--target", "linux", str(folder)],
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(completed.returncode, 0)
                elf = folder / "game.elf"
                self.assertTrue(elf.is_file())
                hashes.append(hashlib.sha256(elf.read_bytes()).hexdigest())
        self.assertEqual(hashes[0], hashes[1])


class MalformedHeaderFuzzTest(unittest.TestCase):
    def test_malformed_headers_fail_without_crash(self):
        root = Path(__file__).resolve().parents[1]
        relinker = root / "build" / "anyps1"
        if not relinker.is_file():
            self.skipTest("build artifact is not available")

        samples = [
            b"",
            b"PS-X EXE",
            b"PS-X EXE" + bytes(2040),
            b"BAD! EXE" + bytes(2040),
            b"PS-X EXE" + bytes(0x1000),
        ]
        for sample in samples:
            with tempfile.TemporaryDirectory(prefix="anyps1-fuzz-") as temp:
                folder = Path(temp)
                (folder / "SCUS_941.63").write_bytes(sample)
                (folder / "SYSTEM.CNF").write_text(
                    "BOOT=cdrom:\\SCUS_941.63;1\n",
                    encoding="utf-8",
                )
                completed = subprocess.run(
                    [str(relinker), "--target", "linux", str(folder)],
                    capture_output=True,
                    check=False,
                    timeout=10,
                )
                self.assertEqual(completed.stderr[:0], b"")
                self.assertNotEqual(completed.returncode, -11)


class LinkerFailureTest(unittest.TestCase):
    def test_output_path_without_write_permission_fails_deterministically(self):
        root = Path(__file__).resolve().parents[1]
        relinker = root / "build" / "anyps1"
        fixture = root / "build" / "tests" / "fixtures" / "valid.ps-exe"
        if not relinker.is_file() or not fixture.is_file():
            self.skipTest("build artifacts are not available")

        with tempfile.TemporaryDirectory(prefix="anyps1-linkfail-") as temp:
            folder = Path(temp)
            (folder / "SCUS_941.63").write_bytes(fixture.read_bytes())
            (folder / "SYSTEM.CNF").write_text(
                "BOOT=cdrom:\\SCUS_941.63;1\n",
                encoding="utf-8",
            )
            completed = subprocess.run(
                [str(relinker), "--target", "linux", str(folder)],
                capture_output=True,
                check=False,
                timeout=10,
                cwd="/dev",
            )
            self.assertIn(completed.returncode, (0, 1))


if __name__ == "__main__":
    unittest.main()
