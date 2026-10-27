// FUSE glue: translates libfuse3 callbacks into memfs::FileSystem calls.
//
// Usage:  ./memfs <mountpoint> [-f] [-d] [-s]
//   -f  stay in the foreground     -d  debug (prints every FUSE request)
//   -s  single-threaded
// Unmount with:  fusermount3 -u <mountpoint>
#define FUSE_USE_VERSION 31

#include <fuse.h>

#include <unistd.h>

#include "memfs.hpp"

namespace {

memfs::FileSystem* fs = nullptr;

// uid/gid of the process making the current request (e.g. your shell).
uid_t caller_uid() { return fuse_get_context()->uid; }
gid_t caller_gid() { return fuse_get_context()->gid; }

int op_getattr(const char* path, struct stat* st, fuse_file_info*) {
    return fs->getattr(path, st);
}

int op_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t, fuse_file_info*,
               fuse_readdir_flags) {
    return fs->readdir(path, [&](const std::string& name, const struct stat& st) {
        filler(buf, name.c_str(), &st, 0, static_cast<fuse_fill_dir_flags>(0));
    });
}

int op_mkdir(const char* path, mode_t mode) {
    return fs->mkdir(path, mode, caller_uid(), caller_gid());
}

int op_rmdir(const char* path) { return fs->rmdir(path); }

int op_create(const char* path, mode_t mode, fuse_file_info*) {
    return fs->create(path, mode, caller_uid(), caller_gid());
}

int op_unlink(const char* path) { return fs->unlink(path); }

int op_rename(const char* from, const char* to, unsigned int flags) {
    return fs->rename(from, to, flags);
}

int op_symlink(const char* target, const char* linkpath) {
    return fs->symlink(target, linkpath, caller_uid(), caller_gid());
}

int op_readlink(const char* path, char* buf, size_t size) {
    return fs->readlink(path, buf, size);
}

int op_open(const char* path, fuse_file_info*) { return fs->open(path); }

int op_read(const char* path, char* buf, size_t size, off_t offset, fuse_file_info*) {
    return fs->read(path, buf, size, offset);
}

int op_write(const char* path, const char* buf, size_t size, off_t offset, fuse_file_info*) {
    return fs->write(path, buf, size, offset);
}

int op_truncate(const char* path, off_t size, fuse_file_info*) {
    return fs->truncate(path, size);
}

int op_chmod(const char* path, mode_t mode, fuse_file_info*) { return fs->chmod(path, mode); }

int op_chown(const char* path, uid_t uid, gid_t gid, fuse_file_info*) {
    return fs->chown(path, uid, gid);
}

int op_utimens(const char* path, const timespec tv[2], fuse_file_info*) {
    return fs->utimens(path, tv);
}

int op_statfs(const char*, struct statvfs* st) {
    // Pretend we have 1 GiB so `df` shows something sensible.
    constexpr unsigned long kBlock = 4096;
    constexpr unsigned long kTotal = (1UL << 30) / kBlock;
    unsigned long used = (fs->bytes_used() + kBlock - 1) / kBlock;
    *st = {};
    st->f_bsize = st->f_frsize = kBlock;
    st->f_blocks = kTotal;
    st->f_bfree = st->f_bavail = used < kTotal ? kTotal - used : 0;
    st->f_namemax = 255;
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    // The mount's root directory is owned by whoever ran memfs.
    memfs::FileSystem filesystem(getuid(), getgid());
    fs = &filesystem;

    fuse_operations ops{};
    ops.getattr = op_getattr;
    ops.readdir = op_readdir;
    ops.mkdir = op_mkdir;
    ops.rmdir = op_rmdir;
    ops.create = op_create;
    ops.unlink = op_unlink;
    ops.rename = op_rename;
    ops.symlink = op_symlink;
    ops.readlink = op_readlink;
    ops.open = op_open;
    ops.read = op_read;
    ops.write = op_write;
    ops.truncate = op_truncate;
    ops.chmod = op_chmod;
    ops.chown = op_chown;
    ops.utimens = op_utimens;
    ops.statfs = op_statfs;

    return fuse_main(argc, argv, &ops, nullptr);
}
