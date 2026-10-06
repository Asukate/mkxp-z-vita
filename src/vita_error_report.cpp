#include "vita_error_report.h"
#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

bool vitaWriteErrorReport(const std::string &text, const char *directory) {
    constexpr size_t limit = 32768;
    const std::string path = std::string(directory) + "/last-error.txt";
    const std::string temp = path + ".tmp";
    const std::string previous = path + ".prev";
    mkdir(directory, 0777);
    FILE *file = std::fopen(temp.c_str(), "wb");
    if (!file) return false;
    const size_t length = std::min(text.size(), limit);
    bool ok = std::fwrite(text.data(), 1, length, file) == length;
    if (text.size() > limit) {
        const char suffix[] = "\n[Backtrace truncated]\n";
        ok = std::fwrite(suffix, 1, sizeof(suffix) - 1, file) == sizeof(suffix) - 1 && ok;
    }
    if (std::fclose(file) != 0) ok = false;
    if (!ok) { std::remove(temp.c_str()); return false; }
    // Vita rename does not replace an existing destination. Retain the last
    // complete report until the new one has been fully written.
    FILE *old = std::fopen(path.c_str(), "rb");
    if (old) {
        std::fclose(old);
        std::remove(previous.c_str());
        if (std::rename(path.c_str(), previous.c_str()) != 0) return false;
    }
    return std::rename(temp.c_str(), path.c_str()) == 0;
}
