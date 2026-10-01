//
//  filesystemImpl.cpp
//  Player
//
//  Created by ゾロアーク on 11/21/20.
//

#include <SDL_filesystem.h>

#include "filesystemImpl.h"
#include "util/exception.h"
#include "util/debugwriter.h"

#ifdef MKXPZ_EXP_FS
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#include "ghc/ghc_vita_stubs.h"
#include "ghc/filesystem.hpp"
namespace fs = ghc::filesystem;
#endif

#include <fstream>

#ifdef __vita__
static bool isVitaDevicePath(const std::string &path)
{
    const std::string::size_type separator = path.find(":/");
    return separator != std::string::npos && separator > 0;
}

static std::string normalizeVitaDevicePath(std::string path)
{
    for (char &ch : path)
    {
        if (ch == '\\')
            ch = '/';
    }

    /* Device paths are resolved by sceIo/PhysFS, not by the host-style
     * filesystem implementation.  In particular, `ux0:/data/` is already
     * absolute; feeding it through fs::path while the current directory is
     * `app0:/` creates the invalid `app0:/app0:/ux0:/...` form. */
    std::string::size_type prefixEnd = path.find(":/");
    std::string prefix = path.substr(0, prefixEnd + 2);
    std::string tail = path.substr(prefixEnd + 2);
    std::string normalized;

    for (std::string::size_type start = 0; start <= tail.size();)
    {
        const std::string::size_type end = tail.find('/', start);
        const std::string component = tail.substr(
            start, end == std::string::npos ? std::string::npos : end - start);

        if (component.empty() || component == ".")
        {
            /* Ignore duplicate separators and current-directory segments. */
        }
        else if (component == "..")
        {
            const std::string::size_type slash = normalized.find_last_of('/');
            if (slash == std::string::npos)
                normalized.clear();
            else
                normalized.erase(slash);
        }
        else
        {
            if (!normalized.empty())
                normalized += '/';
            normalized += component;
        }

        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    return prefix + normalized;
}
#endif
bool filesystemImpl::fileExists(const char *path) {
    fs::path stdPath(path);
    return (fs::exists(stdPath) && !fs::is_directory(stdPath));
}


// https://stackoverflow.com/questions/2912520/read-file-contents-into-a-string-in-c
std::string filesystemImpl::contentsOfFileAsString(const char *path) {
    std::string ret;
    try {
        std::ifstream ifs(path);
        ret = std::string ( (std::istreambuf_iterator<char>(ifs) ),
                       (std::istreambuf_iterator<char>()    ) );
    } catch (...) {
        throw Exception(Exception::NoFileError, "Failed to read file at %s", path);
    }

    return ret;
}

// chdir and getcwd do not support unicode on Windows
bool filesystemImpl::setCurrentDirectory(const char *path) {
    // The throwing fs::current_path overload must stay inside the guard:
    // this function promises bool, and a missing directory must report
    // false (so callers can fall back) instead of terminating the process.
    try {
        fs::path stdPath(path);
        fs::current_path(stdPath);
        return fs::equivalent(fs::current_path(), stdPath);
    } catch (...) {
        Debug() << "Failed to switch current path." << path;
        return false;
    }
}

std::string filesystemImpl::getCurrentDirectory() {
    std::string ret;
    try {
        ret = std::string(fs::current_path().string());
    } catch (...) {
        throw Exception(Exception::MKXPError, "Failed to retrieve current path");
    }
    return ret;
}


std::string filesystemImpl::normalizePath(const char *path, bool preferred, bool absolute) {
    if (path && isVitaDevicePath(path))
        return normalizeVitaDevicePath(path);

    fs::path stdPath(path);
    
    if (!stdPath.is_absolute() && absolute)
        stdPath = fs::current_path() / stdPath;

    stdPath = stdPath.lexically_normal();
    std::string ret(stdPath);
    for (size_t i = 0; i < ret.length(); i++) {
        char sep;
        char sep_alt;
#ifdef __WIN32__
        if (preferred) {
            sep = '\\';
            sep_alt = '/';
        }
        else
#endif
        {
            sep = '/';
            sep_alt = '\\';
        }
        
        if (ret[i] == sep_alt)
            ret[i] = sep;
    }
    return ret;
}

std::string filesystemImpl::getDefaultGameRoot() {
    char *p = SDL_GetBasePath();
    std::string ret(p);
    SDL_free(p);
    return ret;
}
