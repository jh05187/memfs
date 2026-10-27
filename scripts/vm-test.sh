#!/usr/bin/env bash
# Copy the project to the Linux VM, build it there, and run all tests
# (unit tests + the FUSE smoke test).
#
# Usage: scripts/vm-test.sh user@192.168.64.3
set -euo pipefail

HOST=${1:?usage: $0 user@vm-ip}
ROOT=$(cd "$(dirname "$0")/.." && pwd)

rsync -a --delete --exclude build "$ROOT/" "$HOST:memfs/"
ssh "$HOST" 'set -e
  cd ~/memfs
  cmake -S . -B build -DMEMFS_SANITIZE=ON >/dev/null
  cmake --build build -j
  ctest --test-dir build --output-on-failure'
