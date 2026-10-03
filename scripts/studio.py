"""Serve only the Studio editor on this desktop; no flight-data service."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import argparse
import webbrowser


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parents[1] / "dist"
    handler = partial(SimpleHTTPRequestHandler, directory=str(directory))
    with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
        url = f"http://localhost:{server.server_port}"
        print(f"Flightdeck Studio: {url}\nOpen in desktop Chrome or Edge. Ctrl+C closes the editor host; the ESP32 keeps running.", flush=True)
        if not args.no_browser:
            webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
