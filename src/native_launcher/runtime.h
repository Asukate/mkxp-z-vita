#pragma once
#include <string>
bool nativeLauncherDirectBoot();
bool nativeLauncherRequested(int argc, char **argv);
int runNativeLauncher(bool returning);
