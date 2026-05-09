#include "resource_manager.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include "error.h"
#include "filesystem.h"
#include "sound.h"
#include "tools.h"

#ifndef STBI_NO_STDIO
#define STBI_NO_STDIO
#endif
#ifndef STBI_NO_GIF
#define STBI_NO_GIF
#endif
#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include "../../external/SDL/src/video/stb_image.h"

namespace TA::resmgr {
    const std::filesystem::path externalModsRoot = "E:/TailsAdventureRemake/mods";

    struct Mod {
        std::filesystem::path root;
        std::vector<std::filesystem::path> files;
        int priority = 0;
        bool enabled = false;
    };

    void loadMods();
    Mod loadMod(std::filesystem::path root, const std::map<std::string, bool>* iniEnabled = nullptr);
    std::filesystem::path getAssetPath(std::filesystem::path asset);
    std::vector<std::filesystem::path> getModRoots();
    std::map<std::string, bool> ensureAndLoadExternalModIni(
        const std::filesystem::path& modsRoot, const std::vector<std::filesystem::path>& modDirs);
    bool isEnabledByMarker(const std::filesystem::path& modRoot);
    std::string trim(std::string value);
    std::string lowercase(std::string value);
    bool isExternalModsRoot(const std::filesystem::path& path);

    void preloadTextures();
    void preloadChunks();
    SDL_Texture* createFallbackTexture();

    std::unordered_map<std::string, std::filesystem::path> overrides;
    int totalMods = 0;
    int loadedMods = 0;

    std::unordered_map<std::string, SDL_Texture*> textureMap;
#if !defined(TA_DISABLE_AUDIO)
    std::unordered_map<std::string, MIX_Audio*> musicMap;
    std::unordered_map<std::string, MIX_Audio*> chunkMap;
#endif
    std::unordered_map<std::string, std::string> assetMap;
    std::unordered_map<std::string, toml::value> tomlMap;
}

SDL_Texture* TA::resmgr::createFallbackTexture() {
    static Uint32 pixel = 0xFF00FFFF; // RGBA8888 magenta
    SDL_Surface* surface = SDL_CreateSurfaceFrom(1, 1, SDL_PIXELFORMAT_RGBA8888, &pixel, sizeof(pixel));
    if(surface == nullptr) {
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(TA::renderer, surface);
    SDL_DestroySurface(surface);
    if(texture != nullptr) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    return texture;
}

void TA::resmgr::load() {
    loadMods();
    preloadTextures();
    preloadChunks();
}

void TA::resmgr::loadMods() {
    overrides.clear();
    totalMods = 0;
    loadedMods = 0;

    const std::vector<std::filesystem::path> modRoots = getModRoots();
    std::vector<Mod> mods;
    std::set<std::string> seenModDirs;

    for(const auto& modsPath : modRoots) {
        std::error_code ec;
        if(!std::filesystem::is_directory(modsPath, ec) || ec) {
            continue;
        }

        std::vector<std::filesystem::path> modDirs;

        const bool externalModsRootPath = isExternalModsRoot(modsPath);
        std::error_code dirItError;
        std::filesystem::directory_iterator dirIt(
            modsPath, std::filesystem::directory_options::skip_permission_denied, dirItError);
        std::filesystem::directory_iterator dirEnd;
        for(; dirIt != dirEnd; dirIt.increment(dirItError)) {
            if(dirItError) {
                dirItError.clear();
                continue;
            }

            const std::filesystem::path modPath = dirIt->path();
            std::error_code pathError;
            if(!std::filesystem::is_directory(modPath, pathError) || pathError) {
                continue;
            }

            if(!externalModsRootPath) {
                std::error_code enabledError;
                if(!std::filesystem::is_regular_file(modPath / "enabled", enabledError) || enabledError) {
                    continue;
                }
            }

            const std::string key = modPath.lexically_normal().generic_string();
            if(seenModDirs.contains(key)) {
                continue;
            }
            seenModDirs.insert(key);
            modDirs.push_back(modPath);
        }

        std::map<std::string, bool> iniEnabled;
        const std::map<std::string, bool>* iniEnabledPtr = nullptr;
        if(externalModsRootPath && !modDirs.empty()) {
            iniEnabled = ensureAndLoadExternalModIni(modsPath, modDirs);
            iniEnabledPtr = &iniEnabled;
        }

        for(const auto& modDir : modDirs) {
            mods.push_back(loadMod(modDir, iniEnabledPtr));
        }
    }

    if(mods.empty()) {
        return;
    }

    std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) { return a.priority < b.priority; });

    std::vector<std::string> loaded;
    for(const Mod& mod : mods) {
        totalMods++;
        if(!mod.enabled) continue;
        loaded.push_back(mod.root.filename().generic_string());
        loadedMods++;
        for(const std::filesystem::path& path : mod.files) {
            std::filesystem::path relPath = path.lexically_relative(mod.root);
            if(relPath.empty()) {
                const std::string rootStr = mod.root.generic_string();
                const std::string fileStr = path.generic_string();
                if(fileStr.rfind(rootStr, 0) == 0) {
                    size_t offset = rootStr.size();
                    if(offset < fileStr.size() && (fileStr[offset] == '/' || fileStr[offset] == '\\')) {
                        offset++;
                    }
                    relPath = std::filesystem::path(fileStr.substr(offset));
                }
            }
            if(relPath.empty()) {
                continue;
            }
            std::string rel = relPath.generic_string();
            overrides[rel] = path;
        }
    }

    if(!loaded.empty()) {
        std::string log = "loaded mods (highest priority last): ";
        for(const std::string name : loaded) {
            log += name;
            log += ' ';
        }
        TA::printLog("%s", log.c_str());
    }

}

bool TA::resmgr::isExternalModsRoot(const std::filesystem::path& path) {
    return lowercase(path.lexically_normal().generic_string()) ==
        lowercase(externalModsRoot.lexically_normal().generic_string());
}

std::string TA::resmgr::lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}

std::string TA::resmgr::trim(std::string value) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool TA::resmgr::isEnabledByMarker(const std::filesystem::path& modRoot) {
    const std::filesystem::path enabledPath = modRoot / "enabled";
    if(!std::filesystem::is_regular_file(enabledPath)) {
        return false;
    }

    std::string enabledValue = TA::filesystem::readFile(enabledPath);
    return !enabledValue.empty() && enabledValue.front() == '1';
}

std::map<std::string, bool> TA::resmgr::ensureAndLoadExternalModIni(
    const std::filesystem::path& modsRoot, const std::vector<std::filesystem::path>& modDirs) {
    std::map<std::string, bool> enabledMap;
    const std::filesystem::path iniPath = modsRoot / "mods.ini";

    bool hadIni = std::filesystem::is_regular_file(iniPath);
    if(hadIni) {
        std::stringstream iniStream(TA::filesystem::readFile(iniPath));
        std::string line;
        while(std::getline(iniStream, line)) {
            line = trim(line);
            if(line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }
            size_t eq = line.find('=');
            if(eq == std::string::npos) {
                continue;
            }
            std::string name = trim(line.substr(0, eq));
            std::string value = trim(line.substr(eq + 1));
            if(name.empty()) {
                continue;
            }
            enabledMap[name] = (value == "1" || lowercase(value) == "true" || lowercase(value) == "on");
        }
    }

    bool changed = !hadIni;
    for(const auto& modDir : modDirs) {
        const std::string modName = modDir.filename().generic_string();
        if(modName.empty()) {
            continue;
        }
        if(!enabledMap.contains(modName)) {
            enabledMap[modName] = isEnabledByMarker(modDir);
            changed = true;
        }
    }

    if(changed) {
        std::stringstream out;
        out << "; Tails Adventure Remake mods config\n";
        out << "; 1=true (enabled), 0=false (disabled)\n";
        out << "; Edit values below and relaunch the game.\n\n";
        for(const auto& [modName, enabled] : enabledMap) {
            out << modName << "=" << (enabled ? "1" : "0") << "\n";
        }
        TA::filesystem::writeFile(iniPath, out.str());
    }

    return enabledMap;
}

std::vector<std::filesystem::path> TA::resmgr::getModRoots() {
#ifdef __ANDROID__
    if(SDL_GetAndroidExternalStorageState() !=
        (SDL_ANDROID_EXTERNAL_STORAGE_READ | SDL_ANDROID_EXTERNAL_STORAGE_WRITE)) {
        return {};
    }
    std::filesystem::path storagePath = SDL_GetAndroidExternalStoragePath();
    return {storagePath / "mods"};
#elif defined(TA_UNIX_INSTALL)
    return {"~/.local/share/tails-adventure/mods"};
#elif defined(SDL_PLATFORM_WINRT)
    const std::filesystem::path writable = TA::filesystem::getWritableDataPath();
    const std::filesystem::path executable = TA::filesystem::getExecutableDirectory();
    return {
        writable / "mods",
        executable / "LocalState" / "mods",
        "E:/TailsAdventureRemake/mods"};
#else
    return {TA::filesystem::getExecutableDirectory() / "mods"};
#endif
}

TA::resmgr::Mod TA::resmgr::loadMod(std::filesystem::path root, const std::map<std::string, bool>* iniEnabled) {
    Mod mod = Mod();
    mod.root = root;

    if(iniEnabled != nullptr) {
        const std::string modName = root.filename().generic_string();
        auto iter = iniEnabled->find(modName);
        if(iter != iniEnabled->end()) {
            mod.enabled = iter->second;
        } else {
            mod.enabled = isEnabledByMarker(root);
        }
    } else {
        mod.enabled = isEnabledByMarker(root);
    }

    if(!mod.enabled) {
        return mod;
    }
    std::error_code iterError;
    std::filesystem::recursive_directory_iterator fileIt(
        root, std::filesystem::directory_options::skip_permission_denied, iterError);
    std::filesystem::recursive_directory_iterator fileEnd;
    for(; fileIt != fileEnd; fileIt.increment(iterError)) {
        if(iterError) {
            iterError.clear();
            continue;
        }

        const std::filesystem::path filePath = fileIt->path();
        std::error_code fileError;
        if(std::filesystem::is_regular_file(filePath, fileError) && !fileError) {
            mod.files.emplace_back(filePath);
        }
    }
    std::error_code priorityError;
    if(std::filesystem::is_regular_file(root / "priority", priorityError) && !priorityError) {
        try {
            mod.priority = std::stoi(TA::filesystem::readFile(root / "priority"));
        } catch(...) {
            mod.priority = 0;
        }
    }

    return mod;
}

std::filesystem::path TA::resmgr::getAssetPath(std::filesystem::path asset) {
    if(overrides.contains(asset.generic_string())) {
        return overrides.at(asset.generic_string());
    }
    return TA::filesystem::getAssetsPath() / asset;
}

void TA::resmgr::preloadTextures() {
    const std::vector<std::string> names{"bomb", "enemy_bomb", "enemy_rock", "explosion", "leaf", "nezu_bomb", "ring",
        "rock", "splash", "walker_bullet"};

    for(std::string name : names) {
        loadTexture("objects/" + name + ".png");
    }
}

void TA::resmgr::preloadChunks() {
    const std::vector<std::string> names{"break", "damage", "enter", "explosion", "fall", "find_item", "fly", "hammer",
        "hit", "item_switch", "jump", "land", "open", "remote_robot_fly", "remote_robot_step", "ring", "select_item",
        "select", "shoot", "switch", "teleport"};

    for(std::string name : names) {
        loadChunk("sound/" + name + ".ogg");
    }
}

SDL_Texture* TA::resmgr::loadTexture(std::filesystem::path path) {
    path = getAssetPath(path);

    if(!textureMap.count(path.generic_string())) {
        std::string pathStr = path.generic_string();
        size_t bytes = 0;
        char* raw = (char*)SDL_LoadFile(pathStr.c_str(), &bytes);
        if(raw == nullptr || bytes == 0) {
#ifdef SDL_PLATFORM_WINRT
            SDL_Log("texture load fallback (%s): %s", pathStr.c_str(), SDL_GetError());
            SDL_Texture* fallback = createFallbackTexture();
            if(fallback == nullptr) {
                TA::handleSDLError("%s", "failed to create fallback texture");
            }
            textureMap[path.generic_string()] = fallback;
            if(raw != nullptr) {
                SDL_free(raw);
            }
            return textureMap[path.generic_string()];
#else
            TA::handleSDLError("open %s for read failed", path.c_str());
#endif
        }

        std::string fileData(raw, bytes);
        SDL_free(raw);
        int imageWidth = 0;
        int imageHeight = 0;
        int imageComponents = 0;
        stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(fileData.data()),
            static_cast<int>(fileData.size()), &imageWidth, &imageHeight, &imageComponents, STBI_rgb_alpha);
        if(pixels == nullptr) {
            const char* decodeError = SDL_GetError();
            if(decodeError == nullptr || decodeError[0] == '\0') {
                decodeError = "unknown image decode error";
            }
#ifdef SDL_PLATFORM_WINRT
            SDL_Log("texture decode fallback (%s): %s", pathStr.c_str(), decodeError);
            SDL_Texture* fallback = createFallbackTexture();
            if(fallback == nullptr) {
                TA::handleSDLError("%s", "failed to create fallback texture");
            }
            textureMap[path.generic_string()] = fallback;
            return textureMap[path.generic_string()];
#else
            TA::handleError("%s: failed to decode image (%s)", pathStr.c_str(), decodeError);
#endif
        }
        SDL_Surface* surface = SDL_CreateSurfaceFrom(
            imageWidth, imageHeight, SDL_PIXELFORMAT_RGBA32, pixels, imageWidth * 4);
        if(surface == nullptr) {
            stbi_image_free(pixels);
            TA::handleSDLError("%s", "failed to create surface from decoded image");
        }
        SDL_Texture* texture = SDL_CreateTextureFromSurface(TA::renderer, surface);
        if(texture == nullptr) {
            SDL_DestroySurface(surface);
            stbi_image_free(pixels);
            TA::handleSDLError("%s", "failed to create texture from surface");
        }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        textureMap[path.generic_string()] = texture;
        SDL_DestroySurface(surface);
        stbi_image_free(pixels);
    }

    return textureMap[path.generic_string()];
}

MIX_Audio* TA::resmgr::loadMusic(std::filesystem::path path) {
#if defined(TA_DISABLE_AUDIO)
    (void)path;
    return nullptr;
#else
    if(TA::sound::getMixer() == nullptr) {
        return nullptr;
    }
    path = getAssetPath(path);

    if(!musicMap.count(path.generic_string())) {
        std::string pathStr = path.generic_string();
        musicMap[path.generic_string()] = MIX_LoadAudio(TA::sound::getMixer(), pathStr.c_str(), false);
        if(musicMap[path.generic_string()] == nullptr) {
            SDL_Log("audio warning: %s load failed: %s", pathStr.c_str(), SDL_GetError());
            return nullptr;
        }
    }

    return musicMap[path.generic_string()];
#endif
}

MIX_Audio* TA::resmgr::loadChunk(std::filesystem::path path) {
#if defined(TA_DISABLE_AUDIO)
    (void)path;
    return nullptr;
#else
    if(TA::sound::getMixer() == nullptr) {
        return nullptr;
    }
    path = getAssetPath(path);

    if(!chunkMap.contains(path.generic_string())) {
        std::string pathStr = path.generic_string();
        chunkMap[path.generic_string()] = MIX_LoadAudio(TA::sound::getMixer(), pathStr.c_str(), true);
        if(chunkMap[path.generic_string()] == nullptr) {
            SDL_Log("audio warning: %s load failed: %s", pathStr.c_str(), SDL_GetError());
            return nullptr;
        }
    }

    return chunkMap[path.generic_string()];
#endif
}

const std::string& TA::resmgr::loadAsset(std::filesystem::path path) {
    path = getAssetPath(path);
    if(!assetMap.contains(path.generic_string())) {
        assetMap[path.generic_string()] = TA::filesystem::readFile(path);
    }
    return assetMap[path.generic_string()];
}

const toml::value& TA::resmgr::loadToml(std::filesystem::path path) {
    path = getAssetPath(path);
    if(!tomlMap.contains(path.generic_string())) {
        try {
            tomlMap[path.generic_string()] = toml::parse_str(TA::filesystem::readFile(path));
        } catch(std::exception& e) {
            TA::handleError("failed to load %s\n%s", path.c_str(), e.what());
        }
    }
    return tomlMap[path.generic_string()];
}

int TA::resmgr::getLoadedMods() {
    return loadedMods;
}

int TA::resmgr::getTotalMods() {
    return totalMods;
}

void TA::resmgr::quit() {
    for(std::pair<std::string, SDL_Texture*> element : textureMap) {
        SDL_DestroyTexture(element.second);
    }
#if !defined(TA_DISABLE_AUDIO)
    for(std::pair<std::string, MIX_Audio*> element : musicMap) {
        MIX_DestroyAudio(element.second);
    }
    for(std::pair<std::string, MIX_Audio*> element : chunkMap) {
        MIX_DestroyAudio(element.second);
    }
#endif
}
