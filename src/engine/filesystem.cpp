#include "filesystem.h"
#include <filesystem>
#include <vector>
#include "SDL3/SDL.h"
#include "SDL3/SDL_iostream.h"
#include "error.h"

#ifdef __APPLE__
#include "mach-o/dyld.h"
#endif
#ifdef _WIN32
#include "windows.h"
#elif __linux__
#include <unistd.h>
#include <climits>
#endif

bool TA::filesystem::fileExists(std::filesystem::path path) {
    std::string pathStr = path.string();
    SDL_IOStream* file = SDL_IOFromFile(pathStr.c_str(), "rb");
    if(file == nullptr) {
        return false;
    }

    if(!SDL_CloseIO(file)) {
        TA::handleSDLError("close %s after checking existence failed", path.c_str());
    }
    return true;
}

std::string TA::filesystem::readFile(std::filesystem::path path) {
    std::string pathStr = path.string();
    size_t dataBytes = 0;
    char* data = (char*)SDL_LoadFile(pathStr.c_str(), &dataBytes);
    if(data == nullptr) {
        TA::handleSDLError("open %s for read failed", path.c_str());
    }
    std::string str(data, dataBytes);
    SDL_free(data);
    return str;
}

std::string TA::filesystem::readAsset(std::filesystem::path path) {
    return readFile(getAssetsPath() / path);
}

std::filesystem::path TA::filesystem::getAssetsPath() {
#ifdef SDL_PLATFORM_WINRT
    static std::filesystem::path cached;
    if(!cached.empty()) {
        return cached;
    }

    const std::filesystem::path base = getExecutableDirectory();
    const std::vector<std::filesystem::path> candidates{
        base / "assets",
        base / "AppX" / "assets",
        std::filesystem::path("assets"),
        std::filesystem::path("AppX") / "assets"};

    for(const auto& candidate : candidates) {
        if(fileExists(candidate / "default_config")) {
            cached = candidate;
            return cached;
        }
    }

    cached = base / "assets";
    return cached;
#endif

#ifdef __ANDROID__
    return "";
#elif defined(TA_UNIX_INSTALL)
    return "/usr/local/share/tails-adventure";
#else
    return getExecutableDirectory() / "assets";
#endif
}

std::filesystem::path TA::filesystem::getExecutableDirectory() {
#ifdef SDL_PLATFORM_WINRT
    const char* basePath = SDL_GetBasePath();
    if(basePath == nullptr) {
        TA::handleSDLError("%s", "failed to get base path");
    }

    std::filesystem::path path(basePath);
    return path;
#elif defined(_WIN32)
    char buffer[MAX_PATH];
    GetModuleFileName(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    return path.substr(0, path.find_last_of("\\/"));
#else
    char buffer[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = PATH_MAX;
    _NSGetExecutablePath(buffer, &size);
    std::string path(buffer);
#else
    ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
    std::string path(buffer, (count > 0 ? count : 0));
#endif
    return path.substr(0, path.find_last_of("/"));
#endif
}

std::filesystem::path TA::filesystem::getWritableDataPath() {
#ifdef SDL_PLATFORM_WINRT
    const char* prefPath = SDL_GetPrefPath("", "tails-adventure");
    if(prefPath != nullptr && prefPath[0] != '\0') {
        std::filesystem::path path(prefPath);
        SDL_free((void*)prefPath);
        std::error_code dirError;
        std::filesystem::create_directories(path, dirError);
        if(!dirError) {
            return path;
        }
        TA::printWarning("create writable pref path failed (%s), trying legacy path", dirError.message().c_str());
    }

    const char* legacyPrefPath = SDL_GetPrefPath("mechakotik", "tails-adventure");
    if(legacyPrefPath != nullptr && legacyPrefPath[0] != '\0') {
        std::filesystem::path legacyPath(legacyPrefPath);
        SDL_free((void*)legacyPrefPath);
        std::error_code legacyError;
        std::filesystem::create_directories(legacyPath, legacyError);
        if(!legacyError) {
            return legacyPath;
        }
        TA::printWarning("legacy writable pref path failed (%s), using base path fallback", legacyError.message().c_str());
    }

    const char* basePath = SDL_GetBasePath();
    if(basePath != nullptr && basePath[0] != '\0') {
        return std::filesystem::path(basePath);
    }
    return ".";
#else
    return getExecutableDirectory();
#endif
}

void TA::filesystem::writeFile(std::filesystem::path path, std::string value) {
    std::string pathStr = path.string();
    SDL_IOStream* file = SDL_IOFromFile(pathStr.c_str(), "wb");
    if(file == nullptr) {
#ifdef SDL_PLATFORM_WINRT
        TA::printWarning("open %s for write failed: %s", path.c_str(), SDL_GetError());
        return;
#else
        TA::handleSDLError("open %s for write failed", path.c_str());
#endif
    }

    for(int pos = 0; pos < (int)value.size(); pos++) {
        SDL_WriteIO(file, &value[pos], 1);
    }

    if(!SDL_CloseIO(file)) {
#ifdef SDL_PLATFORM_WINRT
        TA::printWarning("close %s after writing failed: %s", path.c_str(), SDL_GetError());
#else
        TA::handleSDLError("close %s after writing failed", path.c_str());
#endif
    }
}
