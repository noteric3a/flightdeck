import os

import uvicorn
from dotenv import load_dotenv

load_dotenv()
uvicorn.run(
    "backend.main:create_app",
    factory=True,
    host="0.0.0.0",
    port=int(os.getenv("PORT", "8000")),
    workers=1,
    log_level="info",
    access_log=False,
)
