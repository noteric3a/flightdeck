"""Verify the production entry point over HTTP, including the device simulator."""

import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

from backend.models import ROOT


def test_service_process_and_device_simulator(tmp_path):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    env = {
        **os.environ,
        "PORT": str(port),
        "ADMIN_TOKEN": "local-test-admin-123456",
        "DEVICE_TOKEN": "local-test-device-654321",
        "FLIGHT_PROVIDER": "demo",
        "DATABASE_PATH": str(tmp_path / "service.db"),
        "OPENSKY_CLIENT_ID": "",
        "OPENSKY_CLIENT_SECRET": "",
    }
    with (tmp_path / "service.log").open("w+") as log:
        process = subprocess.Popen(
            [sys.executable, "-m", "backend"], cwd=ROOT, env=env, stdout=log, stderr=log
        )
        try:
            url = f"http://127.0.0.1:{port}"
            for _ in range(60):
                if process.poll() is not None:
                    log.seek(0)
                    raise AssertionError(log.read())
                try:
                    with urllib.request.urlopen(
                        url + "/api/health", timeout=0.5
                    ) as response:
                        assert json.load(response)["status"] == "ok"
                    break
                except (urllib.error.URLError, TimeoutError):
                    time.sleep(0.1)
            else:
                raise AssertionError("Service did not become healthy")
            output = tmp_path / "frame.ppm"
            result = subprocess.run(
                [
                    sys.executable,
                    "scripts/simulate_device.py",
                    "--url",
                    url,
                    "--frames",
                    "1",
                    "--output",
                    str(output),
                ],
                cwd=ROOT,
                env=env,
                text=True,
                capture_output=True,
                timeout=30,
            )
            assert result.returncode == 0, result.stderr
            assert "CRC valid" in result.stdout
            assert output.read_bytes().startswith(b"P6\n64 32\n255\n")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
