#!/usr/bin/env python3
"""The audit pages, served from the repository's root so they read game/web as it is.

Also serves two folders outside the repository when they exist, for the hub's links:
  /_candidates/   ~/Desktop/sound-sources/candidates   (candidate sounds and their audition page)
  /_sound-audit/  ~/Desktop/sound-audit                (the punch list and its spectrograms)
"""
import functools
import http.server
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXTRA = {
    "/_candidates/": os.path.expanduser("~/Desktop/sound-sources/candidates"),
    "/_sound-audit/": os.path.expanduser("~/Desktop/sound-audit"),
}


class Handler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        for prefix, folder in EXTRA.items():
            if path.startswith(prefix) and os.path.isdir(folder):
                # (the base class keeps the path inside the directory it is given)
                self.directory = folder
                try:
                    return super().translate_path("/" + path[len(prefix):])
                finally:
                    self.directory = ROOT
        return super().translate_path(path)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")  # the files change under the page
        super().end_headers()

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    print(f"Audit:   http://localhost:{port}/tools/audit/")
    print(f"Sprites: http://localhost:{port}/tools/audit/sprites.html")
    print(f"Sounds:  http://localhost:{port}/tools/audit/sounds.html")
    http.server.ThreadingHTTPServer.request_queue_size = 128  # (a page asks for some sixty files at once)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=ROOT))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
