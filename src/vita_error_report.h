#ifndef HARDRPG_VITA_ERROR_REPORT_H
#define HARDRPG_VITA_ERROR_REPORT_H

#include <string>

// Persist before restarting: the launcher presents the report using its
// own context, after the failed game's resources have been released.
bool vitaWriteErrorReport(const std::string &text,
                         const char *directory = "ux0:/data/hardrpg");

#endif
