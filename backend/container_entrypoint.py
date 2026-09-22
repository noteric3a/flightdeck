"""Make a newly mounted data directory writable, then drop container privileges."""

import os
import sys
from pathlib import Path

if os.geteuid() == 0:
    directory = Path("/app/data")
    directory.mkdir(exist_ok=True)
    os.chown(directory, 10001, 10001)
    database = Path(os.getenv("DATABASE_PATH", "/app/data/flightdeck.db"))
    if database.parent == directory:
        for path in (
            database,
            Path(str(database) + "-wal"),
            Path(str(database) + "-shm"),
        ):
            if path.exists() and not path.is_symlink():
                os.chown(path, 10001, 10001)
    os.setgroups([])
    os.setgid(10001)
    os.setuid(10001)
os.execv(sys.executable, [sys.executable, "-m", "backend"])
