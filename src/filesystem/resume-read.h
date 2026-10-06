#ifndef HARDRPG_RESUME_READ_H
#define HARDRPG_RESUME_READ_H
#include <cstring>
#include <cstdint>

// An engine-owned read handle retains its virtual path and logical byte
// position. A failed reopen never discards the original descriptor or marks
// the new epoch as repaired. Writes and Ruby File objects are outside this
// contract; reopening them could truncate user data or change game semantics.
template<class Handle, class IO> struct ResumeRead {
    Handle file;
    char path[512]{};
    int64_t position = 0;
    unsigned epoch;
    ResumeRead(Handle handle, const char *name, unsigned generation)
        : file(handle), epoch(generation) {
        std::strncpy(path, name, sizeof(path) - 1);
    }
    bool reopen(int64_t target) {
        Handle fresh = IO::open(path);
        if (!fresh) return false;
        if (target < 0 || (target && !IO::seek(fresh, target))) {
            IO::close(fresh);
            return false;
        }
        IO::close(file);
        file = fresh;
        position = target;
        return true;
    }
    bool ensure(unsigned generation) {
        if (epoch == generation) return true;
        if (!reopen(position)) return false;
        epoch = generation;
        return true;
    }
};
#endif
