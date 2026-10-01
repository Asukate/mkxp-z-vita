/* Vita platform backend for PhysicsFS (clean-room, generic).
 *
 * Upstream PhysicsFS has no Vita backend: its unix backend derives the base
 * dir from argv0//proc (absent on Vita, so init fails) and uses XDG pref
 * dirs. This backend uses generic Vita mount points without hardcoded
 * game names or application Title IDs:
 *
 *   base dir : "app0:/" (read-only application package)
 *   pref dir : "ux0:/data/<org>/<app>/" with filename-safe sanitization,
 *              with directories created on demand.
 *
 * Mutexes, file I/O, enumeration and symlinks come from the standard
 * physfs_platform_posix.c backend, which is retained alongside this file.
 * There is no CD-ROM support on Vita.
 */

#define __PHYSICSFS_INTERNAL__
#include "physfs_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int __PHYSFS_platformInit(void)
{
    return 1;  /* always succeed. */
} /* __PHYSFS_platformInit */


void __PHYSFS_platformDeinit(void)
{
    /* no-op. */
} /* __PHYSFS_platformDeinit */


void __PHYSFS_platformDetectAvailableCDs(PHYSFS_StringCallback cb, void *data)
{
    (void) cb;
    (void) data;
    /* no-op: no CD-ROM support on Vita. */
} /* __PHYSFS_platformDetectAvailableCDs */


char *__PHYSFS_platformCalcBaseDir(const char *argv0)
{
    (void) argv0;  /* Vita has no argv; the package dir is always app0:/. */
    return __PHYSFS_strdup("app0:/");
} /* __PHYSFS_platformCalcBaseDir */


static void vita_sanitize_component(const char *in, char *out, size_t n)
{
    size_t w = 0;
    size_t i;
    if (in == NULL)
        in = "";
    for (i = 0; in[i] != '\0' && w + 1 < n && w < 64; i++)
    {
        const char c = in[i];
        const int ok = ((c >= 'A') && (c <= 'Z')) ||
                       ((c >= 'a') && (c <= 'z')) ||
                       ((c >= '0') && (c <= '9')) ||
                       (c == '.') || (c == '_') || (c == '-');
        out[w++] = ok ? c : '_';
    } /* for */
    out[w] = '\0';
    if ((w == 0) ||
        ((w == 1) && (out[0] == '.')) ||
        ((w == 2) && (out[0] == '.') && (out[1] == '.')))
        out[0] = '\0';
} /* vita_sanitize_component */


static void vita_mkdir_one(const char *path)
{
    mkdir(path, 0777);
} /* vita_mkdir_one */


char *__PHYSFS_platformCalcPrefDir(const char *org, const char *app)
{
    char saneOrg[72];
    char saneApp[72];
    char base[160];
    char full[200];
    char *retval;

    vita_sanitize_component(org, saneOrg, sizeof (saneOrg));
    vita_sanitize_component(app, saneApp, sizeof (saneApp));
    if (saneOrg[0] == '\0')
        strcpy(saneOrg, "mkxp-z-vita");
    if (saneApp[0] == '\0')
        strcpy(saneApp, "unknown");

    snprintf(base, sizeof (base), "ux0:/data/%s", saneOrg);
    snprintf(full, sizeof (full), "ux0:/data/%s/%s", saneOrg, saneApp);
    vita_mkdir_one("ux0:/data");
    vita_mkdir_one(base);
    vita_mkdir_one(full);

    retval = (char *) allocator.Malloc(strlen(full) + 2);
    if (retval == NULL)
        return NULL;
    strcpy(retval, full);
    strcat(retval, "/");
    return retval;
} /* __PHYSFS_platformCalcPrefDir */
