import sqlite3
import threading
from pathlib import Path

from .models import Configuration, defaults


class RevisionConflict(Exception):
    pass


class ConfigStore:
    def __init__(self, path):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.db = sqlite3.connect(str(path), check_same_thread=False, timeout=5)
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute(
            "CREATE TABLE IF NOT EXISTS display_config (id INTEGER PRIMARY KEY CHECK(id=1), revision INTEGER NOT NULL, body TEXT NOT NULL)"
        )
        with self.db:
            self.db.execute(
                "INSERT OR IGNORE INTO display_config VALUES (1,1,?)",
                (defaults().model_dump_json(),),
            )

    def read(self):
        with self.lock:
            revision, body = self.db.execute(
                "SELECT revision,body FROM display_config WHERE id=1"
            ).fetchone()
        return Configuration.model_validate_json(body), revision

    def write(self, config, expected):
        with self.lock, self.db:
            cursor = self.db.execute(
                "UPDATE display_config SET revision=revision+1,body=? WHERE id=1 AND revision=?",
                (config.model_dump_json(), expected),
            )
            if cursor.rowcount != 1:
                raise RevisionConflict()
        return expected + 1

    def close(self):
        self.db.close()
