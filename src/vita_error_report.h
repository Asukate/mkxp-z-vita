#ifndef HARDRPG_VITA_ERROR_REPORT_H
#define HARDRPG_VITA_ERROR_REPORT_H
#include <cstddef>
#include <string>

// Fixed report storage is reserved before gameplay. Normal sampling is opt-in.
size_t vitaMemorySnapshot(char *destination, size_t capacity);
void vitaErrorSessionStart(const char *gameFolder, int rgssVersion);
void vitaErrorImageStart();
void vitaErrorMemoryEvent(const char *phase, const char *filename);
void vitaErrorReportBegin();
void vitaErrorReportAppend(const char *text, size_t size);
void vitaErrorReportFormat(const char *format, ...);
bool vitaErrorReportSave(const char *directory = "ux0:/data/hardrpg");
bool vitaWriteErrorReport(const std::string &text,
                         const char *directory = "ux0:/data/hardrpg");
#endif
