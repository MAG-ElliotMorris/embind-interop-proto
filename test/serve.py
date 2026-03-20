"""
Simple HTTP server with Cross-Origin headers required for SharedArrayBuffer.

WASM pthreads require SharedArrayBuffer, which browsers only enable when
the page is served with:
  Cross-Origin-Opener-Policy: same-origin
  Cross-Origin-Embedder-Policy: require-corp

Usage (from project root):
  python test/serve.py

Then open: http://localhost:8080/test/test.html
"""

import http.server
import os
import sys

PORT = 8080


class COOPCOEPHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def guess_type(self, path):
        if path.endswith(".wasm"):
            return "application/wasm"
        if path.endswith(".mjs"):
            return "application/javascript"
        return super().guess_type(path)


if __name__ == "__main__":
    # Serve from project root regardless of where script is invoked
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(project_root)

    print(f"Serving {project_root} on http://localhost:{PORT}")
    print(f"Open: http://localhost:{PORT}/test/test.html")
    print("(COOP/COEP headers enabled for SharedArrayBuffer)")

    server = http.server.HTTPServer(("", PORT), COOPCOEPHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.shutdown()
