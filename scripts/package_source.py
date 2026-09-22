"""Package source files while excluding known local state and credential paths.

This is an exclusion policy, not a general-purpose secret scanner. Review the
result before publishing. Importing this module does not create an archive.
"""

from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parent.parent
EXCLUDED_DIRS = frozenset({
    ".git", ".venv", "venv", ".pio", "__pycache__", ".pytest_cache",
    ".ruff_cache", ".mypy_cache", ".openai", "data", "node_modules",
    "htmlcov", "build",
})
EXCLUDED_NAMES = frozenset({
    "secrets.h", "flightdeck-source.zip", "frame.ppm", ".DS_Store",
    "Thumbs.db", ".coverage",
})
EXCLUDED_SUFFIXES = (
    ".pyc", ".pyo", ".db", ".db-shm", ".db-wal", ".sqlite",
    ".sqlite-shm", ".sqlite-wal", ".sqlite3", ".sqlite3-shm", ".sqlite3-wal",
    ".log",
)


def include_source_file(relative: Path) -> bool:
    """Return whether a project-relative file belongs in a source archive."""
    if relative.is_absolute() or ".." in relative.parts:
        return False
    if any(part in EXCLUDED_DIRS for part in relative.parts[:-1]):
        return False
    name = relative.name
    if name in EXCLUDED_NAMES or name.endswith(EXCLUDED_SUFFIXES):
        return False
    if (name == ".env" or name.startswith(".env.")) and name != ".env.example":
        return False
    return True


def build_archive(root: Path = ROOT, target: Path | None = None) -> tuple[Path, int]:
    """Create the portable ZIP and return its path and included file count."""
    root = root.resolve()
    if not root.is_dir():
        raise NotADirectoryError(root)
    target = target or root / "dist" / "flightdeck-source.zip"
    if target.is_symlink():
        raise ValueError("Refusing a symlink archive target")
    target = target.resolve()
    target.parent.mkdir(parents=True, exist_ok=True)
    source_files = []
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if not include_source_file(relative):
            continue
        if path.is_symlink() or not path.is_file() or path.resolve() == target:
            continue
        if any(parent.is_symlink() for parent in path.parents if parent != root):
            continue
        source_files.append((path, relative))
    with ZipFile(target, "w", compression=ZIP_DEFLATED) as archive:
        for path, relative in source_files:
            archive.write(path, "flightdeck/" + relative.as_posix())
    return target, len(source_files)


def main() -> None:
    target, count = build_archive()
    print(f"Packaged {count} project files into {target.name}")


if __name__ == "__main__":
    main()
