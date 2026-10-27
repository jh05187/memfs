// Unit tests for the filesystem tree. No FUSE needed, so these run on macOS too.
#include <cerrno>
#include <cstdio>
#include <set>
#include <string>

#include "memfs.hpp"

static int failures = 0;

#define CHECK_EQ(actual, expected)                                                  \
    do {                                                                            \
        auto a_ = (actual);                                                         \
        auto e_ = (expected);                                                       \
        if (!(a_ == e_)) {                                                          \
            std::fprintf(stderr, "%s:%d: CHECK_EQ(%s, %s) failed\n", __FILE__,      \
                         __LINE__, #actual, #expected);                             \
            ++failures;                                                             \
        }                                                                           \
    } while (0)

static std::string read_all(memfs::FileSystem& fs, const char* path) {
    char buf[256];
    int n = fs.read(path, buf, sizeof(buf), 0);
    return n < 0 ? "<err>" : std::string(buf, n);
}

static std::set<std::string> list(memfs::FileSystem& fs, const char* path) {
    std::set<std::string> names;
    fs.readdir(path, [&](const std::string& name, const struct stat&) { names.insert(name); });
    names.erase(".");
    names.erase("..");
    return names;
}

static void test_files() {
    memfs::FileSystem fs;
    CHECK_EQ(fs.create("/hello.txt", 0644, 0, 0), 0);
    CHECK_EQ(fs.create("/hello.txt", 0644, 0, 0), -EEXIST);
    CHECK_EQ(fs.write("/hello.txt", "hello", 5, 0), 5);
    CHECK_EQ(fs.write("/hello.txt", " world", 6, 5), 6);
    CHECK_EQ(read_all(fs, "/hello.txt"), std::string("hello world"));

    struct stat st;
    CHECK_EQ(fs.getattr("/hello.txt", &st), 0);
    CHECK_EQ(st.st_size, 11);
    CHECK_EQ(S_ISREG(st.st_mode), true);
    CHECK_EQ(static_cast<unsigned>(st.st_mode & 0777), 0644u);

    // Offset reads and reading past EOF.
    char buf[8];
    CHECK_EQ(fs.read("/hello.txt", buf, 5, 6), 5);
    CHECK_EQ(std::string(buf, 5), std::string("world"));
    CHECK_EQ(fs.read("/hello.txt", buf, 5, 100), 0);

    // Writing past the end leaves a zero-filled hole.
    CHECK_EQ(fs.write("/hello.txt", "!", 1, 13), 1);
    CHECK_EQ(read_all(fs, "/hello.txt"), std::string("hello world\0\0!", 14));

    CHECK_EQ(fs.truncate("/hello.txt", 5), 0);
    CHECK_EQ(read_all(fs, "/hello.txt"), std::string("hello"));

    CHECK_EQ(fs.chmod("/hello.txt", 0600), 0);
    fs.getattr("/hello.txt", &st);
    CHECK_EQ(static_cast<unsigned>(st.st_mode & 0777), 0600u);
    CHECK_EQ(S_ISREG(st.st_mode), true);

    CHECK_EQ(fs.unlink("/hello.txt"), 0);
    CHECK_EQ(fs.getattr("/hello.txt", &st), -ENOENT);
    CHECK_EQ(fs.unlink("/hello.txt"), -ENOENT);
}

static void test_dirs() {
    memfs::FileSystem fs;
    CHECK_EQ(fs.mkdir("/a", 0755, 0, 0), 0);
    CHECK_EQ(fs.mkdir("/a/b", 0755, 0, 0), 0);
    CHECK_EQ(fs.mkdir("/missing/c", 0755, 0, 0), -ENOENT);
    CHECK_EQ(fs.create("/a/f", 0644, 0, 0), 0);
    CHECK_EQ(fs.mkdir("/a/f/x", 0755, 0, 0), -ENOTDIR);

    CHECK_EQ(list(fs, "/a"), (std::set<std::string>{"b", "f"}));

    struct stat st;
    fs.getattr("/a", &st);
    CHECK_EQ(st.st_nlink, 3u);  // ".", entry in "/", and "b/.."

    CHECK_EQ(fs.rmdir("/a"), -ENOTEMPTY);
    CHECK_EQ(fs.rmdir("/a/f"), -ENOTDIR);
    CHECK_EQ(fs.unlink("/a/b"), -EISDIR);
    CHECK_EQ(fs.rmdir("/a/b"), 0);
    CHECK_EQ(fs.unlink("/a/f"), 0);
    CHECK_EQ(fs.rmdir("/a"), 0);
    CHECK_EQ(list(fs, "/").empty(), true);
}

static void test_rename() {
    memfs::FileSystem fs;
    fs.mkdir("/src", 0755, 0, 0);
    fs.mkdir("/dst", 0755, 0, 0);
    fs.create("/src/f", 0644, 0, 0);
    fs.write("/src/f", "data", 4, 0);

    CHECK_EQ(fs.rename("/src/f", "/dst/g", 0), 0);
    CHECK_EQ(read_all(fs, "/dst/g"), std::string("data"));
    CHECK_EQ(list(fs, "/src").empty(), true);

    // Replacing an existing file, and RENAME_NOREPLACE.
    fs.create("/dst/h", 0644, 0, 0);
    CHECK_EQ(fs.rename("/dst/g", "/dst/h", 1), -EEXIST);
    CHECK_EQ(fs.rename("/dst/g", "/dst/h", 0), 0);
    CHECK_EQ(read_all(fs, "/dst/h"), std::string("data"));

    // Moving a whole directory carries its contents along.
    CHECK_EQ(fs.rename("/dst", "/src/moved", 0), 0);
    CHECK_EQ(read_all(fs, "/src/moved/h"), std::string("data"));

    // Can't move a directory inside itself, or overwrite a non-empty dir.
    CHECK_EQ(fs.rename("/src", "/src/moved/inner", 0), -EINVAL);
    fs.mkdir("/other", 0755, 0, 0);
    CHECK_EQ(fs.rename("/other", "/src", 0), -ENOTEMPTY);
    CHECK_EQ(fs.rename("/other", "/src/moved/h", 0), -ENOTDIR);
}

static void test_symlinks() {
    memfs::FileSystem fs;
    CHECK_EQ(fs.symlink("/some/target", "/link", 0, 0), 0);

    struct stat st;
    fs.getattr("/link", &st);
    CHECK_EQ(S_ISLNK(st.st_mode), true);

    char buf[64];
    CHECK_EQ(fs.readlink("/link", buf, sizeof(buf)), 0);
    CHECK_EQ(std::string(buf), std::string("/some/target"));

    // Truncated to fit, but still NUL-terminated.
    CHECK_EQ(fs.readlink("/link", buf, 5), 0);
    CHECK_EQ(std::string(buf), std::string("/som"));
}

int main() {
    test_files();
    test_dirs();
    test_rename();
    test_symlinks();

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("all unit tests passed");
    return 0;
}
