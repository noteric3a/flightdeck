"""Create local secrets once. Never print tokens or replace an existing .env."""

import os
import secrets
from pathlib import Path

root = Path(__file__).resolve().parent.parent
path = root / ".env"
if path.exists():
    raise SystemExit(".env already exists; leaving it unchanged.")
content = (
    (root / ".env.example")
    .read_text()
    .replace("ADMIN_TOKEN=", "ADMIN_TOKEN=" + secrets.token_urlsafe(32), 1)
    .replace("DEVICE_TOKEN=", "DEVICE_TOKEN=" + secrets.token_urlsafe(32), 1)
)
fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(fd, "w") as file:
    file.write(content)
print(
    "Created .env with separate admin and device tokens. Open it locally to copy the tokens."
)
