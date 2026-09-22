from pathlib import Path
from zipfile import ZipFile

import pytest

from scripts.package_source import build_archive, include_source_file


@pytest.mark.parametrize("name", [
    ".env", ".env.production", "nested/.env.local", "firmware/include/secrets.h",
    "__pycache__/module.pyc", ".venv/bin/python", "firmware/.pio/build/output.bin",
    "local.db-wal", "local.sqlite3-shm", ".DS_Store", "run.log",
    "dist/flightdeck-source.zip", "frame.ppm",
])
def test_private_and_generated_files_are_excluded(name):
    assert not include_source_file(Path(name))


@pytest.mark.parametrize("name", [
    ".env.example", "firmware/include/secrets.example.h", "dist/app.mjs",
    "dist/defaults.json", "dist/logos/UAL.png", "backend/main.py",
])
def test_required_source_and_examples_are_kept(name):
    assert include_source_file(Path(name))


def test_archive_is_portable_and_does_not_include_itself(tmp_path):
    (tmp_path / "dist").mkdir()
    (tmp_path / "dist/app.mjs").write_text("export const demo = true;\n")
    (tmp_path / ".env.production").write_text("DUMMY_TEST_SECRET=not-a-real-secret\n")
    (tmp_path / ".env.example").write_text("TOKEN=\n")
    target, count = build_archive(tmp_path)
    with ZipFile(target) as archive:
        assert set(archive.namelist()) == {
            "flightdeck/dist/app.mjs", "flightdeck/.env.example",
        }
    assert count == 2
    assert build_archive(tmp_path)[1] == 2


def test_symlinked_files_are_not_packaged(tmp_path):
    root = tmp_path / "project"
    root.mkdir()
    outside = tmp_path / "private.txt"
    outside.write_text("not for publication")
    link = root / "innocent.txt"
    try:
        link.symlink_to(outside)
    except (OSError, NotImplementedError):
        pytest.skip("Symlinks unavailable on this platform")
    target, count = build_archive(root)
    assert count == 0
    with ZipFile(target) as archive:
        assert archive.namelist() == []


def test_paths_cannot_escape_the_project():
    assert not include_source_file(Path("../secrets.txt"))
    assert not include_source_file(Path("/etc/passwd"))
