#!/usr/bin/env bash
# Role B sanity experiment: jammer power and jammer-to-receiver distance.
set -euo pipefail

: "${NS3_DIR:?Set NS3_DIR to your ns-3.47 checkout}"
if [[ ! -x "$NS3_DIR/ns3" ]]; then
  echo "NS3_DIR does not contain an ns-3 launcher: $NS3_DIR" >&2
  exit 1
fi

mkdir -p results
mkdir -p "$NS3_DIR/scratch/v2v-jam"
# Keep the staging area intact; update only this role's named source files.
cp scratch/v2v-jam/main.cc scratch/v2v-jam/jammer.h scratch/v2v-jam/jammer.cc \
  "$NS3_DIR/scratch/v2v-jam/"
output=results/pdr-sweep.csv
printf 'jammer_mode,jammer_power_dbm,distance_m,tx,rx\n' > "$output"

for power in 8 14 20 26; do
  for distance in 10 30 60 100; do
    line=$(cd "$NS3_DIR" && ./ns3 run "scratch/v2v-jam/main --jammer=constant --jammerPowerDbm=$power --jammerDistanceM=$distance")
    result=$(printf '%s\n' "$line" | awk -F, '/^PDR_RESULT/ { print $2 "," $3 "," $4 "," $5 "," $6 }')
    if [[ -z "$result" ]]; then
      echo "No PDR_RESULT for power=$power distance=$distance" >&2
      exit 1
    fi
    printf '%s\n' "$result" >> "$output"
  done
done

python3 analysis/plot_pdr_sanity.py "$output" --out-dir results
echo "Wrote $output, results/pdr-vs-power.png, and results/pdr-vs-distance.png"
