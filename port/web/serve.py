#!/usr/bin/env python3
"""Serve the browser build with the isolation headers Emscripten pthreads need."""
import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class BrowserHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", default="build/web/engine")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    directory = (ROOT / args.directory).resolve()
    if not directory.is_dir():
        parser.error("build directory does not exist: " + str(directory))
    handler = partial(BrowserHandler, directory=str(directory))
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print("Serving " + str(directory) + " at http://%s:%d/" % (args.host, args.port))
    print("COOP/COEP headers enabled for WebAssembly pthreads.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping browser build server.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
