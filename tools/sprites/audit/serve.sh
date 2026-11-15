#!/bin/sh
# The sprite audit page, served from the repository's root so it reads game/web/sprites as they are.
#   tools/sprites/audit/serve.sh [port]     then open http://localhost:8765/tools/sprites/audit/
port="${1:-8765}"
cd "$(dirname "$0")/../../.." || exit 1
echo "Sprite audit: http://localhost:$port/tools/sprites/audit/"
exec python3 -m http.server "$port" --bind 127.0.0.1
