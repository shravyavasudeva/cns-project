#!/usr/bin/env bash
# sync_code.sh - Copy the latest scratch/v2v-jam code into ns-3 and rebuild.
# Use this every time you change (or git pull) the C++ code.
#
#     bash scripts/sync_code.sh            # ns-3 in ~/ns-3.47
#     bash scripts/sync_code.sh /path/to/ns-3.47
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NS3_DIR="${1:-$HOME/ns-3.47}"
rsync -a --delete "$PROJECT_DIR/scratch/v2v-jam/" "$NS3_DIR/scratch/v2v-jam/"
cd "$NS3_DIR"
./ns3 build
echo "Rebuilt. Run with: cd $NS3_DIR && ./ns3 run \"v2v-jam --PrintHelp\""
