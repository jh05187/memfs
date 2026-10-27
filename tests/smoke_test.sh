#!/usr/bin/env bash
# Mounts memfs and drives it with ordinary shell commands.
# Usage: tests/smoke_test.sh path/to/memfs
set -euo pipefail

MEMFS=${1:?usage: $0 path/to/memfs}
MNT=$(mktemp -d)

cleanup() {
    fusermount3 -u "$MNT" 2>/dev/null || true
    rmdir "$MNT" 2>/dev/null || true
}
trap cleanup EXIT

"$MEMFS" "$MNT" -f &
# Wait for the mount to appear.
for _ in $(seq 50); do mountpoint -q "$MNT" && break; sleep 0.1; done
mountpoint -q "$MNT" || { echo "FAIL: mount did not appear"; exit 1; }

step() { echo "  - $*"; }
expect() { [[ "$1" == "$2" ]] || { echo "FAIL: expected '$2', got '$1'"; exit 1; }; }

cd "$MNT"

step "write and read a file"
echo "hello" > a.txt
expect "$(cat a.txt)" "hello"

step "append"
echo "world" >> a.txt
expect "$(cat a.txt)" $'hello\nworld'

step "nested directories"
mkdir -p dir/sub/deep
echo "x" > dir/sub/deep/file
expect "$(cat dir/sub/deep/file)" "x"

step "ls"
expect "$(ls | tr '\n' ' ')" "a.txt dir "

step "rename across directories"
mv a.txt dir/sub/b.txt
expect "$(cat dir/sub/b.txt)" $'hello\nworld'
[[ ! -e a.txt ]]

step "symlink"
ln -s dir/sub/b.txt link
expect "$(readlink link)" "dir/sub/b.txt"
expect "$(head -1 link)" "hello"

step "truncate and chmod"
truncate -s 3 dir/sub/b.txt
expect "$(cat dir/sub/b.txt)" "hel"
chmod 600 dir/sub/b.txt
expect "$(stat -c %a dir/sub/b.txt)" "600"

step "copy a larger file"
head -c 1000000 /dev/urandom > /tmp/memfs_big
cp /tmp/memfs_big big
cmp /tmp/memfs_big big
rm /tmp/memfs_big

step "remove everything"
rm -r dir link big
expect "$(ls -A)" ""

cd /
echo "smoke test passed"
