#pragma once

#include <SDL.h>
#include <algorithm>
#include <cstring>
#include <memory>

// Buffered input for Marshal's small reads, including encrypted archives.
class DataStream {
public:
    explicit DataStream(SDL_RWops *ops)
        : ops(ops), buffer(new unsigned char[bufferSize]) {}
    ~DataStream() { close(); }
    DataStream(const DataStream &) = delete;
    DataStream &operator=(const DataStream &) = delete;

    bool closed() const { return !ops; }
    Sint64 position() const {
        Sint64 position = SDL_RWtell(ops);
        return position < 0 ? position : position - Sint64(available - cursor);
    }
    void close() {
        if (!ops) return;
        SDL_RWclose(ops);
        SDL_FreeRW(ops);
        ops = nullptr;
        buffer.reset();
        cursor = available = 0;
    }
    Sint64 remaining() const {
        Sint64 size = SDL_RWsize(ops);
        Sint64 position = SDL_RWtell(ops);
        if (size < 0 || position < 0) return -1;
        return size - position + Sint64(available - cursor);
    }
    size_t read(void *destination, size_t length) {
        auto out = static_cast<unsigned char *>(destination);
        size_t total = 0;
        while (total < length) {
            if (cursor == available && length - total >= bufferSize) {
                // A big read goes straight to its destination in one call: in 32 KB pieces
                // every piece was its own seek and read on the memory card (BLACK SOULS II's
                // 3 MB Tilesets took 840 ms that way).
                size_t got = SDL_RWread(ops, out + total, 1, length - total);
                if (!got) break;
                total += got;
                continue;
            }
            if (cursor == available) {
                cursor = 0;
                available = SDL_RWread(ops, buffer.get(), 1, bufferSize);
                if (!available) break;
            }
            size_t count = std::min(length - total, available - cursor);
            std::memcpy(out + total, buffer.get() + cursor, count);
            cursor += count;
            total += count;
        }
        return total;
    }
    int getByte() {
        unsigned char byte;
        return read(&byte, 1) == 1 ? byte : -1;
    }

private:
    static constexpr size_t bufferSize = 32768;
    SDL_RWops *ops;
    std::unique_ptr<unsigned char[]> buffer;
    size_t cursor = 0, available = 0;
};
