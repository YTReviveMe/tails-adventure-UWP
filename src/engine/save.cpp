#include "save.h"
#include <filesystem>
#include <map>
#include <sstream>
#include "error.h"
#include "filesystem.h"

namespace TA {
    namespace save {
        void addOptionsFromFile(std::filesystem::path path);
        void ensureDefaultOptions();
        std::filesystem::path getSaveFileName();
        std::map<std::string, long long> saveMap;
        std::string currentSave = "";
    }
}

void TA::save::load() {
    std::filesystem::path defaultConfigPath = TA::filesystem::getAssetsPath() / "default_config";
    addOptionsFromFile(defaultConfigPath);
    addOptionsFromFile(getSaveFileName());
    ensureDefaultOptions();
}

void TA::save::ensureDefaultOptions() {
    auto setIfMissing = [](const std::string& key, long long value) {
        if(!saveMap.contains(key)) {
            saveMap[key] = value;
        }
    };

    setIfMissing("base_height", 0);
    setIfMissing("window_size", 4);
    setIfMissing("pixel_ar", 1);
    setIfMissing("vsync", 1);
    setIfMissing("scale_mode", 0);
    setIfMissing("hide_onscreen", 0);
    setIfMissing("rumble", 1);
    setIfMissing("frame_time", 0);
    setIfMissing("main_volume", 8);
    setIfMissing("music_volume", 8);
    setIfMissing("sfx_volume", 8);
    setIfMissing("ring_drop", 0);

    setIfMissing("keyboard_map_up", 82);
    setIfMissing("keyboard_map_down", 81);
    setIfMissing("keyboard_map_left", 80);
    setIfMissing("keyboard_map_right", 79);
    setIfMissing("keyboard_map_a", 29);
    setIfMissing("keyboard_map_b", 6);
    setIfMissing("keyboard_map_lb", 4);
    setIfMissing("keyboard_map_rb", 7);
    setIfMissing("keyboard_map_start", 40);

    setIfMissing("gamepad_map_a", 0);
    setIfMissing("gamepad_map_b", 1);
    setIfMissing("gamepad_map_lb", 9);
    setIfMissing("gamepad_map_rb", 10);
    setIfMissing("gamepad_map_start", 6);

    setIfMissing("default_save/item_mask", 17);
    setIfMissing("default_save/area_mask", 1099511627779LL);
    setIfMissing("default_save/boss_mask", 0);
    setIfMissing("default_save/rings", 12);
    setIfMissing("default_save/item_slot0", 0);
    setIfMissing("default_save/item_slot1", -1);
    setIfMissing("default_save/item_slot2", -1);
    setIfMissing("default_save/item_slot3", -1);
    setIfMissing("default_save/item_position", 0);
    setIfMissing("default_save/seafox", 0);
    setIfMissing("default_save/seafox_item_slot0", 4);
    setIfMissing("default_save/seafox_item_slot1", -1);
    setIfMissing("default_save/seafox_item_slot2", -1);
    setIfMissing("default_save/seafox_item_slot3", -1);
    setIfMissing("default_save/seafox_item_position", 0);
    setIfMissing("default_save/map_selection", 0);
    setIfMissing("default_save/time", 0);
    setIfMissing("default_save/last_unlocked", 1);
    setIfMissing("default_save/underwater_barrier_passed", 0);
}

void TA::save::addOptionsFromFile(std::filesystem::path path) {
    if(!TA::filesystem::fileExists(path)) {
        TA::printWarning("save file %s was not found, skipping", path.c_str());
        return;
    }

    std::string options = TA::filesystem::readFile(path);
    std::stringstream stream;
    stream << options;

    std::string name;
    long long value;
    while(stream >> name >> value) {
        if(name.starts_with("default_save/") && saveMap.contains(name)) {
            continue;
        }
        saveMap[name] = value;
    }
}

void TA::save::writeToFile() {
    std::stringstream output;
    for(auto [key, value] : saveMap) {
        output << key << ' ' << value << std::endl;
    }

    std::filesystem::path name = getSaveFileName();
    TA::filesystem::writeFile(name, output.str());
}

std::filesystem::path TA::save::getSaveFileName() {
#ifdef __ANDROID__
    const char* path = nullptr;
    if(SDL_GetAndroidExternalStorageState() ==
        (SDL_ANDROID_EXTERNAL_STORAGE_READ | SDL_ANDROID_EXTERNAL_STORAGE_WRITE)) {
        path = SDL_GetAndroidExternalStoragePath();
    }
    if(path != nullptr) {
        return std::filesystem::path(path) / "config";
    }
    return std::filesystem::path(SDL_GetAndroidInternalStoragePath()) / "config";
#elif defined(TA_UNIX_INSTALL)
    std::filesystem::path path = std::filesystem::path(getenv("HOME")) / ".local/share/tails-adventure";
    std::filesystem::create_directories(path);
    return path / "config";
#elif defined(SDL_PLATFORM_WINRT)
    return TA::filesystem::getWritableDataPath() / "config";
#else
    return TA::filesystem::getExecutableDirectory() / "config";
#endif
}

long long TA::save::getParameter(std::string name) {
    if(!saveMap.contains(name)) {
        TA::handleError("unknown parameter %s", name.c_str());
    }
    return saveMap[name];
}

void TA::save::setParameter(std::string name, long long value) {
    saveMap[name] = value;
}

void TA::save::setCurrentSave(std::string name) {
    currentSave = name;
}

long long TA::save::getSaveParameter(std::string name, std::string saveName) {
    if(saveName == "") {
        saveName = currentSave;
    }
    return getParameter(saveName + "/" + name);
}

void TA::save::setSaveParameter(std::string name, long long value, std::string saveName) {
    if(saveName == "") {
        saveName = currentSave;
    }
    setParameter(saveName + "/" + name, value);
}

void TA::save::createSave(std::string saveName) {
    std::map<std::string, long long> newSaveMap = saveMap;
    const std::string defaultSaveName = "default_save/";

    for(auto item : saveMap) {
        if(item.first.length() >= defaultSaveName.length() &&
            item.first.substr(0, defaultSaveName.length()) == defaultSaveName) {
            std::string itemName =
                saveName + "/" +
                item.first.substr(defaultSaveName.length(), item.first.length() - defaultSaveName.length());
            newSaveMap[itemName] = item.second;
        }
    }

    saveMap = newSaveMap;
}

void TA::save::repairSave(std::string saveName) {
    std::map<std::string, long long> newSaveMap = saveMap;
    const std::string defaultSaveName = "default_save/";

    for(auto item : saveMap) {
        if(item.first.length() >= defaultSaveName.length() &&
            item.first.substr(0, defaultSaveName.length()) == defaultSaveName) {
            std::string itemName =
                saveName + "/" +
                item.first.substr(defaultSaveName.length(), item.first.length() - defaultSaveName.length());
            if(!newSaveMap.count(itemName)) {
                newSaveMap[itemName] = item.second;
            }
        }
    }

    saveMap = newSaveMap;
}

bool TA::save::saveExists(int save) {
    std::string saveName = "save_" + std::to_string(save);
    return saveMap.count(saveName + "/item_mask");
}
