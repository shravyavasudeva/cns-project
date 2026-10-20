#!/usr/bin/env bash
set -euo pipefail

: "${NS3_DIR:?Set NS3_DIR to your ns-3.47 checkout, e.g. /home/shrav/ns-3.47}"

if [[ ! -x "$NS3_DIR/ns3" ]]; then
  echo "NS3_DIR does not contain an ns-3 launcher: $NS3_DIR" >&2
  exit 1
fi

mkdir -p results
cp scratch/vanet-jamming.cc "$NS3_DIR/scratch/vanet-jamming.cc"
(
  cd "$NS3_DIR"
  ./ns3 run "scratch/vanet-jamming --vehicles=15 --jammer=true"
)
