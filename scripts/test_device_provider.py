"""Run native provider tests using the exact ArduinoJson installed by PlatformIO."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
libraries = list((root / "firmware/.pio/libdeps").glob("*/ArduinoJson/src/ArduinoJson.h"))
if not libraries:
    raise SystemExit("Run pio pkg install -d firmware first.")
with tempfile.TemporaryDirectory(prefix="flightdeck-provider-") as temp:
    target = Path(temp) / "provider"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-fsanitize=address,undefined", "-g",
                    "-I", str(root / "firmware/include"), "-I", str(libraries[0].parent),
                    str(root / "tests/aeroapi_native.cpp"), "-o", str(target)], check=True)
    subprocess.run([str(target)], check=True)
print("Native AeroAPI parsing, units, timestamps, filtering and identity checks passed.")
