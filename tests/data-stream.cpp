#include "binding/data-stream.h"
#include <cassert>
#include <iostream>
#include <vector>

struct Fixture {
    std::vector<unsigned char> bytes;
    size_t position = 0, chunk = 19, reads = 0, closes = 0;
};
static Fixture &fixture(SDL_RWops *ops) {
    return *static_cast<Fixture *>(ops->hidden.unknown.data1);
}
static SDL_RWops *open(Fixture &f) {
    auto ops = SDL_AllocRW();
    ops->hidden.unknown.data1 = &f;
    ops->size = [](SDL_RWops *o) -> Sint64 { return fixture(o).bytes.size(); };
    ops->seek = [](SDL_RWops *o, Sint64 offset, int whence) -> Sint64 {
        auto &f = fixture(o);
        Sint64 next = offset + (whence == RW_SEEK_CUR ? f.position :
                               whence == RW_SEEK_END ? f.bytes.size() : 0);
        if (next < 0 || size_t(next) > f.bytes.size()) return -1;
        return f.position = size_t(next);
    };
    ops->read = [](SDL_RWops *o, void *out, size_t size, size_t count) -> size_t {
        auto &f = fixture(o); ++f.reads;
        size_t bytes = std::min({size * count, f.chunk, f.bytes.size() - f.position});
        bytes -= bytes % size;
        std::memcpy(out, f.bytes.data() + f.position, bytes);
        f.position += bytes;
        return bytes / size;
    };
    ops->close = [](SDL_RWops *o) -> int { ++fixture(o).closes; return 0; };
    return ops;
}
int main() {
    Fixture f;
    for (size_t i = 0; i < 100003; ++i) f.bytes.push_back(i % 251);
    {
        DataStream s(open(f));
        for (int i = 0; i < 103; ++i) assert(s.getByte() == int(f.bytes[i]));
        assert(s.position() == 103 && s.remaining() == 99900);
        std::vector<unsigned char> remaining(110000, 255);
        assert(s.read(remaining.data(), remaining.size()) == 99900);
        assert(std::equal(f.bytes.begin() + 103, f.bytes.end(), remaining.begin()));
        assert(remaining[99900] == 255);
        assert(s.getByte() == -1 && s.remaining() == 0);
        s.close(); s.close();
        assert(s.closed() && f.closes == 1);
    }
    assert(f.closes == 1);
    Fixture buffered; buffered.chunk = 32768; buffered.bytes = f.bytes;
    {
        DataStream s(open(buffered));
        for (auto value : buffered.bytes) assert(s.getByte() == value);
        assert(buffered.reads == 4);
    }
    assert(buffered.closes == 1);
    std::cout << "PASS: short reads, mixed byte/block reads, buffered position, EOF and single close\n";
}
