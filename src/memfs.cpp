#include "memfs.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

namespace memfs {
namespace {

constexpr size_t kMaxName = 255;

timespec now() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts;
}

// "/a//b/c/" -> {"a", "b", "c"}
std::vector<std::string_view> split(std::string_view path) {
    std::vector<std::string_view> parts;
    size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') ++i;
        size_t j = i;
        while (j < path.size() && path[j] != '/') ++j;
        if (j > i) parts.push_back(path.substr(i, j - i));
        i = j;
    }
    return parts;
}

void touch(Node& n) {
    n.mtime = n.ctime = now();
}

void fill_stat(const Node& n, struct stat* st) {
    std::memset(st, 0, sizeof(*st));
    st->st_mode = n.mode;
    st->st_uid = n.uid;
    st->st_gid = n.gid;
    st->st_blksize = 4096;

    if (n.is_dir()) {
        // "." and the entry in the parent, plus each subdirectory's "..".
        st->st_nlink = 2 + std::count_if(n.children.begin(), n.children.end(),
                                         [](const auto& kv) { return kv.second->is_dir(); });
        st->st_size = 4096;
    } else {
        st->st_nlink = 1;
        st->st_size = static_cast<off_t>(n.data.size());
    }
    st->st_blocks = (st->st_size + 511) / 512;

#ifdef __APPLE__
    st->st_atimespec = n.atime;
    st->st_mtimespec = n.mtime;
    st->st_ctimespec = n.ctime;
#else
    st->st_atim = n.atime;
    st->st_mtim = n.mtime;
    st->st_ctim = n.ctime;
#endif
}

std::unique_ptr<Node> make_node(mode_t mode, uid_t uid, gid_t gid) {
    auto n = std::make_unique<Node>();
    n->mode = mode;
    n->uid = uid;
    n->gid = gid;
    n->atime = n->mtime = n->ctime = now();
    return n;
}

}  // namespace

FileSystem::FileSystem(uid_t uid, gid_t gid)
    : root_(make_node(S_IFDIR | 0755, uid, gid)) {}

// ---- private helpers (caller holds mu_) ----

Node* FileSystem::lookup(std::string_view path) {
    Node* n = root_.get();
    for (auto part : split(path)) {
        if (!n->is_dir()) return nullptr;
        auto it = n->children.find(part);
        if (it == n->children.end()) return nullptr;
        n = it->second.get();
    }
    return n;
}

int FileSystem::parent_of(std::string_view path, Node*& parent, std::string& name) {
    auto parts = split(path);
    if (parts.empty()) return -EEXIST;  // the root has no parent
    if (parts.back().size() > kMaxName) return -ENAMETOOLONG;

    Node* n = root_.get();
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        auto it = n->children.find(parts[i]);
        if (it == n->children.end()) return -ENOENT;
        n = it->second.get();
        if (!n->is_dir()) return -ENOTDIR;
    }
    parent = n;
    name = std::string(parts.back());
    return 0;
}

int FileSystem::add_child(std::string_view path, mode_t mode, uid_t uid, gid_t gid, Node** out) {
    Node* parent = nullptr;
    std::string name;
    if (int err = parent_of(path, parent, name)) return err;
    if (parent->children.count(name)) return -EEXIST;

    auto node = make_node(mode, uid, gid);
    if (out) *out = node.get();
    parent->children.emplace(std::move(name), std::move(node));
    touch(*parent);
    return 0;
}

// ---- metadata ----

int FileSystem::getattr(std::string_view path, struct stat* st) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;
    fill_stat(*n, st);
    return 0;
}

int FileSystem::readdir(std::string_view path, const DirFiller& fill) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;
    if (!n->is_dir()) return -ENOTDIR;

    struct stat st;
    fill_stat(*n, &st);
    fill(".", st);
    fill("..", st);  // the kernel fixes up ".." itself; this is just a placeholder
    for (const auto& [name, child] : n->children) {
        fill_stat(*child, &st);
        fill(name, st);
    }
    n->atime = now();
    return 0;
}

int FileSystem::chmod(std::string_view path, mode_t mode) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;
    n->mode = (n->mode & S_IFMT) | (mode & 07777);
    n->ctime = now();
    return 0;
}

int FileSystem::chown(std::string_view path, uid_t uid, gid_t gid) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;
    // -1 means "leave unchanged", per chown(2).
    if (uid != static_cast<uid_t>(-1)) n->uid = uid;
    if (gid != static_cast<gid_t>(-1)) n->gid = gid;
    n->ctime = now();
    return 0;
}

int FileSystem::utimens(std::string_view path, const timespec tv[2]) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;

    auto apply = [](timespec& dst, const timespec* src) {
        if (!src || src->tv_nsec == UTIME_NOW) dst = now();
        else if (src->tv_nsec != UTIME_OMIT) dst = *src;
    };
    apply(n->atime, tv ? &tv[0] : nullptr);
    apply(n->mtime, tv ? &tv[1] : nullptr);
    n->ctime = now();
    return 0;
}

// ---- directories ----

int FileSystem::mkdir(std::string_view path, mode_t mode, uid_t uid, gid_t gid) {
    std::lock_guard lock(mu_);
    return add_child(path, S_IFDIR | (mode & 07777), uid, gid, nullptr);
}

int FileSystem::rmdir(std::string_view path) {
    std::lock_guard lock(mu_);
    Node* parent = nullptr;
    std::string name;
    if (int err = parent_of(path, parent, name)) return err == -EEXIST ? -EBUSY : err;

    auto it = parent->children.find(name);
    if (it == parent->children.end()) return -ENOENT;
    if (!it->second->is_dir()) return -ENOTDIR;
    if (!it->second->children.empty()) return -ENOTEMPTY;

    parent->children.erase(it);
    touch(*parent);
    return 0;
}

// ---- files and links ----

int FileSystem::create(std::string_view path, mode_t mode, uid_t uid, gid_t gid) {
    std::lock_guard lock(mu_);
    return add_child(path, S_IFREG | (mode & 07777), uid, gid, nullptr);
}

int FileSystem::symlink(std::string_view target, std::string_view linkpath, uid_t uid, gid_t gid) {
    std::lock_guard lock(mu_);
    Node* link = nullptr;
    if (int err = add_child(linkpath, S_IFLNK | 0777, uid, gid, &link)) return err;
    link->data = std::string(target);
    return 0;
}

int FileSystem::readlink(std::string_view path, char* buf, size_t size) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;
    if (!n->is_symlink()) return -EINVAL;
    if (size == 0) return -EINVAL;

    // FUSE wants a NUL-terminated string, truncated if it doesn't fit.
    size_t len = std::min(n->data.size(), size - 1);
    std::memcpy(buf, n->data.data(), len);
    buf[len] = '\0';
    return 0;
}

int FileSystem::unlink(std::string_view path) {
    std::lock_guard lock(mu_);
    Node* parent = nullptr;
    std::string name;
    if (int err = parent_of(path, parent, name)) return err == -EEXIST ? -EISDIR : err;

    auto it = parent->children.find(name);
    if (it == parent->children.end()) return -ENOENT;
    if (it->second->is_dir()) return -EISDIR;

    parent->children.erase(it);
    touch(*parent);
    return 0;
}

int FileSystem::rename(std::string_view from, std::string_view to, unsigned int flags) {
    constexpr unsigned int kNoReplace = 1;  // RENAME_NOREPLACE
    if (flags & ~kNoReplace) return -EINVAL;  // RENAME_EXCHANGE etc. not supported

    std::lock_guard lock(mu_);
    auto from_parts = split(from);
    auto to_parts = split(to);
    if (from_parts.empty() || to_parts.empty()) return -EBUSY;  // can't move the root
    if (from_parts == to_parts) return 0;

    // A directory can't be moved inside itself: "mv /a /a/b".
    if (to_parts.size() > from_parts.size() &&
        std::equal(from_parts.begin(), from_parts.end(), to_parts.begin()))
        return -EINVAL;

    Node *src_parent = nullptr, *dst_parent = nullptr;
    std::string src_name, dst_name;
    if (int err = parent_of(from, src_parent, src_name)) return err;
    if (int err = parent_of(to, dst_parent, dst_name)) return err;

    auto src_it = src_parent->children.find(src_name);
    if (src_it == src_parent->children.end()) return -ENOENT;
    Node* src = src_it->second.get();

    auto dst_it = dst_parent->children.find(dst_name);
    if (dst_it != dst_parent->children.end()) {
        if (flags & kNoReplace) return -EEXIST;
        Node* dst = dst_it->second.get();
        if (src->is_dir() && !dst->is_dir()) return -ENOTDIR;
        if (!src->is_dir() && dst->is_dir()) return -EISDIR;
        if (dst->is_dir() && !dst->children.empty()) return -ENOTEMPTY;
    }

    auto node = std::move(src_it->second);
    src_parent->children.erase(src_it);
    dst_parent->children[dst_name] = std::move(node);  // replaces any old target

    touch(*src_parent);
    touch(*dst_parent);
    src->ctime = now();
    return 0;
}

// ---- file contents ----

int FileSystem::open(std::string_view path) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;
    if (n->is_dir()) return -EISDIR;
    return 0;
}

int FileSystem::read(std::string_view path, char* buf, size_t size, off_t offset) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;
    if (n->is_dir()) return -EISDIR;
    if (offset < 0) return -EINVAL;

    auto off = static_cast<size_t>(offset);
    if (off >= n->data.size()) return 0;  // EOF
    size_t len = std::min(size, n->data.size() - off);
    std::memcpy(buf, n->data.data() + off, len);
    n->atime = now();
    return static_cast<int>(len);
}

int FileSystem::write(std::string_view path, const char* buf, size_t size, off_t offset) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;
    if (n->is_dir()) return -EISDIR;
    if (offset < 0) return -EINVAL;

    auto off = static_cast<size_t>(offset);
    // Writing past the end fills the gap with zeros, like a sparse file.
    if (off + size > n->data.size()) n->data.resize(off + size, '\0');
    std::memcpy(n->data.data() + off, buf, size);
    touch(*n);
    return static_cast<int>(size);
}

int FileSystem::truncate(std::string_view path, off_t size) {
    std::lock_guard lock(mu_);
    Node* n = lookup(path);
    if (!n) return -ENOENT;
    if (n->is_dir()) return -EISDIR;
    if (size < 0) return -EINVAL;

    n->data.resize(static_cast<size_t>(size), '\0');
    touch(*n);
    return 0;
}

size_t FileSystem::bytes_used() {
    std::lock_guard lock(mu_);
    size_t total = 0;
    std::vector<const Node*> stack{root_.get()};
    while (!stack.empty()) {
        const Node* n = stack.back();
        stack.pop_back();
        total += n->data.size();
        for (const auto& kv : n->children) stack.push_back(kv.second.get());
    }
    return total;
}

}  // namespace memfs
