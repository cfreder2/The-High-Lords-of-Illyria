#!/bin/sh
# The audit pages (sprites and sounds), served from the repository's root so they read game/web as it is.
#   tools/audit/serve.sh [port]     then open http://localhost:8765/tools/audit/
exec python3 "$(dirname "$0")/serve.py" "${1:-8765}"
