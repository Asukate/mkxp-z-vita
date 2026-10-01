#ifndef SDL_filesystem_h_
#define SDL_filesystem_h_
#include "SDL_stdinc.h"
#ifdef __cplusplus
extern "C" {
#endif
char *SDL_GetBasePath(void);
char *SDL_GetPrefPath(const char *org, const char *app);
#ifdef __cplusplus
}
#endif
#endif
