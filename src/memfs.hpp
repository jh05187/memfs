// memfs: an in-memory filesystem tree.
//
// This file knows nothing about FUSE. Every method takes a path like
// "/a/b/c", does the operation on the tree, and returns 0 on success or a
// negative errno (e.g. -ENOENT) on failure -- exactly the convention FUSE
// callbacks use, so main.cpp can forward calls straight through.
//
// Keeping it FUSE-free means it compiles and unit-tests on any OS (macOS too).
#pragma once

#include <sys/stat.h>
#include <sys/types.h>

#include <cstddef>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace memfs {

struct Node {
    mode_t mode = 0;  // file type bits (S_IFDIR/S_IFREG/S_IFLNK) | permissions
    uid_t uid = 0;
    gid_t gid = 0;
    timespec atime{}, mtime{}, ctime{};

    std::string data;  // file contents, or the target path for a symlink

    // Directory entries. std::less<> lets us look up by string_view.
    std::map<std::string, std::unique_ptr<Node>, std::less<>> children;

    bool is_dir() const { return S_ISDIR(mode); }
    bool is_file() const { return S_ISREG(mode); }
    bool is_symlink() const { return S_ISLNK(mode); }
};

class FileSystem {
public:
    using DirFiller = std::function<void(const std::string& name, const struct stat& st)>;

    FileSystem(uid_t uid = 0, gid_t gid = 0);

    int getattr(std::string_view path, struct stat* st);
    int readdir(std::string_view path, const DirFiller& fill);

    int mkdir(std::string_view path, mode_t mode, uid_t uid, gid_t gid);
    int rmdir(std::string_view path);
    int create(std::string_view path, mode_t mode, uid_t uid, gid_t gid);
    int unlink(std::string_view path);
    int rename(std::string_view from, std::string_view to, unsigned int flags);
    int symlink(std::string_view target, std::string_view linkpath, uid_t uid, gid_t gid);
    int readlink(std::string_view path, char* buf, size_t size);

    int open(std::string_view path);
    // read/write return the number of bytes transferred (>= 0) or -errno.
    int read(std::string_view path, char* buf, size_t size, off_t offset);
    int write(std::string_view path, const char* buf, size_t size, off_t offset);
    int truncate(std::string_view path, off_t size);

    int chmod(std::string_view path, mode_t mode);
    int chown(std::string_view path, uid_t uid, gid_t gid);
    int utimens(std::string_view path, const timespec tv[2]);

    // Total bytes stored in regular files (handy for statfs / debugging).
    size_t bytes_used();

private:
    Node* lookup(std::string_view path);
    // Finds the directory that should contain `path` and the final component.
    int parent_of(std::string_view path, Node*& parent, std::string& name);
    int add_child(std::string_view path, mode_t mode, uid_t uid, gid_t gid, Node** out);

    std::unique_ptr<Node> root_;
    // FUSE runs callbacks on several threads; one big lock keeps it simple.
    std::mutex mu_;
};

}  // namespace memfs
