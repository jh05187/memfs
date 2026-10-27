# memfs: an in-memory FUSE filesystem in C++

A filesystem that keeps everything in RAM. You mount it on a directory, and
ordinary tools (`ls`, `cat`, `cp`, `mv`, `vim`, …) work in it. Everything is
gone when you unmount.

## How it works

```
 your shell ──► kernel VFS ──► /dev/fuse ──► libfuse3 ──► main.cpp ──► memfs.cpp
  (ls, cat)     (syscalls)                   (callbacks)   (glue)       (the tree)
```

- `src/memfs.hpp / memfs.cpp`: the filesystem itself, a tree of `Node`s.
  Each method takes a path and returns `0` or `-errno`. It doesn't depend on
  FUSE, so it builds and unit-tests on macOS too.
- `src/main.cpp`: about 100 lines that connect each libfuse callback
  (`getattr`, `read`, `write`, `rename`, …) to the tree.
- `tests/memfs_test.cpp`: unit tests for the tree (no mount needed).
- `tests/smoke_test.sh`: mounts the filesystem for real and runs shell
  commands against it.

Supported: files, directories, symlinks, read/write at any offset, sparse
writes, truncate, rename (including `RENAME_NOREPLACE`), chmod, chown,
timestamps, and `statfs` (so `df` works).

## Setting up the Ubuntu VM (one time)

In the Ubuntu VM in UTM:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libfuse3-dev fuse3 openssh-server rsync
```

Then, on the Mac, copy your SSH key across so scripts can log in without a password:

```bash
ssh-copy-id <your-vm-username>@192.168.64.3
```

## Build and run

**On the Mac** (core and unit tests only, since macOS has no Linux FUSE):

```bash
cmake -S . -B build -DMEMFS_SANITIZE=ON && cmake --build build && ctest --test-dir build
```

**On the VM, from the Mac** (syncs the code, builds it, runs every test):

```bash
scripts/vm-test.sh <your-vm-username>@192.168.64.3
```

**Try it by hand inside the VM:**

```bash
mkdir -p ~/mnt
./build/memfs ~/mnt -f -d     # -f foreground, -d prints every FUSE request
# in a second terminal:
cd ~/mnt && echo hi > x && cat x && ls -la && df -h .
fusermount3 -u ~/mnt          # unmount
```

Watching the `-d` output while you run `ls` or `cp` is the best way to see
which syscalls turn into which callbacks.

## Ideas for extending it

Roughly easiest to hardest:

1. **Size limit**: refuse writes with `-ENOSPC` past a `--max-size` option
   (see `fuse_opt_parse`).
2. **Hard links**: needs `link()`, plus reference-counted nodes (`shared_ptr`)
   so two names can point at the same `Node`.
3. **Persistence**: save the tree to a file on unmount (`destroy` callback)
   and load it on mount (`init`).
4. **Encrypted filesystem**: store file data on a real backing directory,
   encrypted with libsodium, so the mount shows plaintext.
5. **Low-level API**: rewrite `main.cpp` against `fuse_lowlevel.h` (inode
   numbers instead of paths). It's faster and closer to how the kernel thinks.
6. **Network filesystem**: serve the tree from a server over TCP, so
   `read`/`write` become RPCs.
