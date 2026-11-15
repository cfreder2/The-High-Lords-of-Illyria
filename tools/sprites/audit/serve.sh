#!/bin/sh
# Moved: the sprite audit is one of the audit pages now (tools/audit). The old address still opens it.
exec "$(dirname "$0")/../../audit/serve.sh" "$@"
