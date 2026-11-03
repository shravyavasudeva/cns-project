#!/usr/bin/env bash
# setup_ns3.sh - Install ns-3.47 inside WSL Ubuntu 22.04 (or any Ubuntu/Debian) and build v2v-jam.
#
# Run from the project folder:
#     bash scripts/setup_ns3.sh            # installs into ~/ns-3.47
#     bash scripts/setup_ns3.sh /opt/ns3   # or choose another parent folder
#
# What it does:
#   1. apt installs the compilers and tools ns-3 needs
#   2. downloads the ns-3.47 source (if not already there)
#   3. copies scratch/v2v-jam into ns-3's scratch/ folder
#   4. configures an optimized build with only the modules we need, and builds
#   5. runs a 15-second smoke test
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PARENT="${1:-$HOME}"
NS3_DIR="$PARENT/ns-3.47"

echo "==> Project: $PROJECT_DIR"
echo "==> ns-3 will be in: $NS3_DIR"

echo "==> 1/5 Installing build tools (needs sudo password)"
sudo apt-get update
sudo apt-get install -y g++ python3 python3-pip python3-venv cmake ninja-build git wget tar bzip2 rsync

if [ ! -x "$NS3_DIR/ns3" ]; then
  echo "==> 2/5 Downloading ns-3.47"
  mkdir -p "$PARENT"
  cd "$PARENT"
  wget -q --show-progress -O ns-3.47.tar.gz \
    https://gitlab.com/nsnam/ns-3-dev/-/archive/ns-3.47/ns-3-dev-ns-3.47.tar.gz
  tar xzf ns-3.47.tar.gz
  mv ns-3-dev-ns-3.47 "$NS3_DIR"
  rm ns-3.47.tar.gz
else
  echo "==> 2/5 ns-3.47 already present, skipping download"
fi

echo "==> 3/5 Copying our code into $NS3_DIR/scratch/v2v-jam"
mkdir -p "$NS3_DIR/scratch/v2v-jam"
rsync -a --delete "$PROJECT_DIR/scratch/v2v-jam/" "$NS3_DIR/scratch/v2v-jam/"

echo "==> 4/5 Configuring and building (first build takes 10-30 minutes)"
cd "$NS3_DIR"
./ns3 configure --build-profile=optimized --disable-werror \
  --enable-modules="core;network;mobility;propagation;spectrum;wifi;netanim"
./ns3 build

echo "==> 5/5 Smoke test (15 simulated seconds)"
./ns3 run "v2v-jam --scenario=highway --jammer=constant --mitigation=3 --vehicles=20 --simTime=15 --alertStart=11 --outDir=results/smoke"
ls -l results/smoke

echo
echo "All good. Next steps (see README.md):"
echo "  pip3 install -r $PROJECT_DIR/requirements.txt"
echo "  python3 $PROJECT_DIR/run_batch.py --ns3 $NS3_DIR --preset sanity"
