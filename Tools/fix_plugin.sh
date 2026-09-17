#!/bin/sh
# .cplugin association on Linux -> validate + auto-fix one plugin.
exec "$(dirname "$0")/maho_python.sh" "$(dirname "$0")/fix_plugin.py" "$@"
