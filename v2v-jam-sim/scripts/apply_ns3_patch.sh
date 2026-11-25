#!/usr/bin/env bash
# apply_ns3_patch.sh - Apply our ns-3.47 bug-fix patch(es) from patches/ and rebuild.
# Safe to run more than once: patches that are already applied are skipped.
#
#     bash scripts/apply_ns3_patch.sh            # ns-3 in ~/ns-3.47
#     bash scripts/apply_ns3_patch.sh /path/to/ns-3.47
#     bash scripts/apply_ns3_patch.sh ~/ns-3.47 --no-build
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NS3_DIR="${1:-$HOME/ns-3.47}"
cd "$NS3_DIR"
for p in "$PROJECT_DIR"/patches/*.patch; do
  if patch -p1 -R --dry-run -s -f < "$p" >/dev/null 2>&1; then
    echo "already applied: $(basename "$p")"
  elif patch -p1 --dry-run -s -f < "$p" >/dev/null 2>&1; then
    patch -p1 -s < "$p"
    echo "applied: $(basename "$p")"
  else
    echo "ERROR: $(basename "$p") does not apply to $NS3_DIR (is this ns-3.47?)" >&2
    exit 1
  fi
done
if [ "${2:-}" != "--no-build" ]; then
  ./ns3 build
fi
