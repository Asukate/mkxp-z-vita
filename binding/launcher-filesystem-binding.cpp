// HardRPG's read-only ZIP view. The engine already owns PhysicsFS; keep
// browser mounts under a private prefix so they cannot replace game assets.
#include <ruby.h>
#include <physfs.h>
#include <cstring>
#include <string>

namespace {
const char *mountPoint = "__hardrpg_zip__";
std::string mountedArchive;
PHYSFS_File *stream = nullptr;

void closeStream() {
    if (stream) PHYSFS_close(stream);
    stream = nullptr;
}

VALUE closeArchive(VALUE) {
    closeStream();
    if (!mountedArchive.empty()) {
        if (!PHYSFS_unmount(mountedArchive.c_str()))
            rb_raise(rb_eIOError, "Could not close ZIP archive");
        mountedArchive.clear();
    }
    return Qnil;
}

void fail(const char *operation) {
    const char *error = PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());
    rb_raise(rb_eIOError, "%s: %s", operation, error ? error : "ZIP error");
}

std::string virtualPath(VALUE value) {
    const char *path = StringValueCStr(value);
    // No absolute paths, mount/device syntax, traversal or Windows separators.
    if (*path == '/' || std::strchr(path, '\\') || std::strchr(path, ':'))
        rb_raise(rb_eArgError, "Invalid ZIP path");
    std::string input(path);
    size_t start = 0;
    while (start < input.size()) {
        size_t end = input.find('/', start);
        std::string part = input.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
            rb_raise(rb_eArgError, "Invalid ZIP path");
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (mountedArchive.empty()) rb_raise(rb_eIOError, "No ZIP archive open");
    return std::string(mountPoint) + (input.empty() ? "" : "/" + input);
}

VALUE openArchive(VALUE self, VALUE path) {
    const char *name = StringValueCStr(path);
    if (mountedArchive == name) return Qtrue;
    closeArchive(self);
    if (!PHYSFS_isInit() && !PHYSFS_init("hardrpg")) fail("Initialize filesystem");
    if (!PHYSFS_mount(name, mountPoint, 0)) fail("Open ZIP");
    mountedArchive = name;
    return Qtrue;
}

VALUE entries(VALUE, VALUE path) {
    std::string full = virtualPath(path);
    char **names = PHYSFS_enumerateFiles(full.c_str());
    if (!names) fail("List ZIP folder");
    VALUE result = rb_ary_new();
    for (char **name = names; *name; ++name) {
        if (!**name || std::strchr(*name, '/') || std::strchr(*name, '\\') ||
            std::strchr(*name, ':') || !std::strcmp(*name, ".") || !std::strcmp(*name, ".."))
            continue;
        PHYSFS_Stat stat;
        std::string child = full + "/" + *name;
        if (!PHYSFS_stat(child.c_str(), &stat)) continue;
        if (stat.filetype != PHYSFS_FILETYPE_DIRECTORY && stat.filetype != PHYSFS_FILETYPE_REGULAR)
            continue;
        VALUE entry = rb_ary_new_from_args(3, rb_utf8_str_new_cstr(*name),
            stat.filetype == PHYSFS_FILETYPE_DIRECTORY ? Qtrue : Qfalse,
            LL2NUM(stat.filesize));
        rb_ary_push(result, entry);
    }
    PHYSFS_freeList(names);
    return result;
}

VALUE streamOpen(VALUE, VALUE path) {
    closeStream();
    std::string full = virtualPath(path);
    stream = PHYSFS_openRead(full.c_str());
    if (!stream) fail("Read ZIP file");
    return LL2NUM(PHYSFS_fileLength(stream));
}

VALUE streamRead(VALUE, VALUE count) {
    long length = NUM2LONG(count);
    if (length < 1 || length > 262144) rb_raise(rb_eArgError, "ZIP read must be 1..262144 bytes");
    if (!stream) rb_raise(rb_eIOError, "No ZIP file open");
    VALUE result = rb_str_new(nullptr, length);
    PHYSFS_sint64 read = PHYSFS_readBytes(stream, RSTRING_PTR(result), length);
    if (read < 0) { closeStream(); fail("Read ZIP file"); }
    rb_str_set_len(result, read);
    return result;
}

VALUE streamClose(VALUE) { closeStream(); return Qnil; }

VALUE setRoot(VALUE, VALUE path) {
    if (mountedArchive.empty()) rb_raise(rb_eIOError, "No ZIP archive open");
    virtualPath(path); // Apply the same relative-path validation as browsing.
    closeStream();
    if (!PHYSFS_setRoot(mountedArchive.c_str(), StringValueCStr(path))) fail("Set ZIP root");
    return Qtrue;
}

VALUE smallRead(VALUE self, VALUE path) {
    VALUE size = streamOpen(self, path);
    if (NUM2LL(size) > 262144) {
        closeStream();
        rb_raise(rb_eIOError, "Game.ini exceeds 256 KiB");
    }
    VALUE result = streamRead(self, LONG2NUM(262144));
    closeStream();
    return result;
}
} // namespace

void launcherFilesystemBindingInit() {
    VALUE mod = rb_define_module("HardRPGArchive");
    rb_define_module_function(mod, "open", RUBY_METHOD_FUNC(openArchive), 1);
    rb_define_module_function(mod, "close", RUBY_METHOD_FUNC(closeArchive), 0);
    rb_define_module_function(mod, "entries", RUBY_METHOD_FUNC(entries), 1);
    rb_define_module_function(mod, "read", RUBY_METHOD_FUNC(smallRead), 1);
    rb_define_module_function(mod, "stream_open", RUBY_METHOD_FUNC(streamOpen), 1);
    rb_define_module_function(mod, "stream_read", RUBY_METHOD_FUNC(streamRead), 1);
    rb_define_module_function(mod, "stream_close", RUBY_METHOD_FUNC(streamClose), 0);
    rb_define_module_function(mod, "set_root", RUBY_METHOD_FUNC(setRoot), 1);
}

// Also compile this exact backend as a host Ruby extension for preview/tests.
extern "C" void Init_hardrpg_archive() { launcherFilesystemBindingInit(); }
