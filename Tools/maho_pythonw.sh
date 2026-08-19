#!/usr/bin/env sh
# GUI helper. On Linux there is no "pythonw" (console hiding is Windows-only),
# so this is an alias of maho_python.sh — kept for parity with the Windows toolchain.
set -u
exec "$(dirname "$0")/maho_python.sh" "$@"
