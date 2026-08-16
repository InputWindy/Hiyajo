#!/usr/bin/env sh
# Scan .cplugin manifests → module dependency / build order JSON.
set -u
cd "$(dirname "$0")/.." || exit 1
exec "$(dirname "$0")/maho_python.sh" "$(dirname "$0")/scan_plugins.py" "$@"
