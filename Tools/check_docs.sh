#!/usr/bin/env sh
# Check all repository .md links (broken-link / stale-doc hygiene).
# Run this after editing docs.
set -u
cd "$(dirname "$0")/.." || exit 1
exec "$(dirname "$0")/maho_python.sh" "$(dirname "$0")/check_md_links.py" "$@"
