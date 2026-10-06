#include "src/vita_error_report.h"
#include "src/vita_resume.h"
#include "src/filesystem/resume-read.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <stdexcept>

struct FakeFile { int64_t pos; bool closed; };
struct FakeIO {
    static bool failOpen, failSeek;
    static FakeFile handles[8];
    static int count;
    static FakeFile *open(const char *) { return failOpen ? nullptr : &handles[count++]; }
    static bool seek(FakeFile *f, int64_t n) { if (failSeek) return false; f->pos = n; return true; }
    static void close(FakeFile *f) { f->closed = true; }
};
bool FakeIO::failOpen = false;
bool FakeIO::failSeek = false;
FakeFile FakeIO::handles[8]{};
int FakeIO::count = 0;
static std::string contents(const std::string &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string dir = argv[1];
    const std::string trace = "NameError\n\nMissing API\n\nBacktrace:\n001:Main:12\n002:Scene:8\n";
    assert(vitaWriteErrorReport(trace, dir.c_str()));
    assert(contents(dir + "/last-error.txt") == trace);
    assert(vitaWriteErrorReport("Second error", dir.c_str()));
    assert(contents(dir + "/last-error.txt.prev") == trace);
    assert(contents(dir + "/last-error.txt") == "Second error");
    assert(vitaWriteErrorReport(std::string(100000, 'x'), dir.c_str()));
    assert(contents(dir + "/last-error.txt").size() < 32800);
    assert(!vitaWriteErrorReport(trace, (dir + "/missing/subdir").c_str()));
    VitaResumeGap gap;
    assert(!gap.sample(100));
    assert(!gap.sample(150));
    assert(!gap.sample(2150));
    assert(gap.sample(5151));
    assert(!gap.sample(5160));
    assert(!gap.sample(1)); // wall-clock adjustment cannot underflow
    unsigned e = vitaResumeEpoch(); vitaResumeNotify(); assert(vitaResumeEpoch() == e + 1);
    FakeFile original{7, false};
    ResumeRead<FakeFile *, FakeIO> read(&original, "Audio/BGM/music.ogg", 0);
    read.position = 7;
    assert(read.ensure(0) && read.file == &original);
    FakeIO::failOpen = true;
    assert(!read.ensure(1) && !original.closed && read.epoch == 0);
    FakeIO::failOpen = false; FakeIO::failSeek = true;
    assert(!read.ensure(1) && !original.closed && read.epoch == 0);
    assert(FakeIO::handles[0].closed);
    FakeIO::failSeek = false;
    assert(read.ensure(1) && original.closed && read.position == 7);
    assert(read.file->pos == 7 && read.epoch == 1);
    FakeFile *first = read.file;
    read.position = 23;
    assert(read.ensure(2) && first->closed && read.file->pos == 23);
}
