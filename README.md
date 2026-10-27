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
  Each method takes a path and returns `0` or `-errno`. It has no FUSE
  dependency, so you can unit-test it without mounting anything.
- `src/main.cpp`: about 100 lines that connect each libfuse callback
  (`getattr`, `read`, `write`, `rename`, …) to the tree.
- `tests/memfs_test.cpp`: unit tests for the tree.
- `tests/smoke_test.sh`: mounts the filesystem for real and runs shell
  commands against it.

Supported: files, directories, symlinks, read/write at any offset, sparse
writes, truncate, rename (including `RENAME_NOREPLACE`), chmod, chown,
timestamps, and `statfs` (so `df` works).

## Requirements

A Linux system with FUSE 3, CMake 3.20 or newer, and a C++20 compiler.

Debian / Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config libfuse3-dev fuse3
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake pkgconf-pkg-config fuse3-devel fuse3
```

Arch:

```bash
sudo pacman -S base-devel cmake pkgconf fuse3
```

## Build and test

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`ctest` runs both the unit tests and the smoke test, which mounts memfs in a
temporary directory.

To build with AddressSanitizer and UndefinedBehaviorSanitizer turned on
(recommended while hacking on it):

```bash
cmake -S . -B build -DMEMFS_SANITIZE=ON
```

## Try it

```bash
mkdir -p ~/mnt
./build/memfs ~/mnt -f -d     # -f foreground, -d prints every FUSE request
```

In a second terminal:

```bash
cd ~/mnt
echo hi > x && cat x
mkdir -p a/b && mv x a/b/
ls -laR
df -h .
```

When you're done, unmount (or press Ctrl+C in the first terminal):

```bash
fusermount3 -u ~/mnt
```

Leave out `-f` to run memfs in the background like a normal daemon. Add `-s`
to make it single-threaded, which makes debugging easier.

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
