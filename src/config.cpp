#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

fs::path Config::configPath() {
#ifdef _WIN32
    // First check for config in same directory as exe (portable mode)
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path portableConfig = exeDir / "config.json";
        if (fs::exists(portableConfig)) {
            return portableConfig;
        }
    }

    // Fall back to AppData
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        fs::path configDir = fs::path(path) / "pleyx";
        fs::create_directories(configDir);
        return configDir / "config.json";
    }
#endif
    return "config.json";
}

Config Config::load() {
    Config cfg;
    fs::path path = configPath();

    if (!fs::exists(path)) {
        saveDefault();
    }

    try {
        std::ifstream file(path);
        if (file.is_open()) {
            json j;
            file >> j;

            cfg.plexUrl = j.value("plex_url", "http://localhost:32400");
            cfg.plexToken = j.value("plex_token", "");
            cfg.plexUsername = j.value("plex_username", "");
            cfg.omdbApiKey = j.value("omdb_api_key", "");
            cfg.pollingIntervalSecs = j.value("polling_interval_secs", 15);
            cfg.startAtBoot = j.value("start_at_boot", false);
            cfg.debug = j.value("debug", false);
        }
    } catch (const std::exception& e) {
        std::cerr << "[Config] Error loading config: " << e.what() << std::endl;
    }

    return cfg;
}

void Config::save() const {
    fs::path path = configPath();

    json j = {
        {"plex_url", plexUrl},
        {"plex_token", plexToken},
        {"polling_interval_secs", pollingIntervalSecs},
        {"start_at_boot", startAtBoot}
    };
    // Only include optional keys if set
    if (!plexUsername.empty()) {
        j["plex_username"] = plexUsername;
    }
    if (!omdbApiKey.empty()) {
        j["omdb_api_key"] = omdbApiKey;
    }
    if (debug) {
        j["debug"] = true;
    }

    try {
        std::ofstream file(path);
        file << j.dump(4);
        std::cout << "[Config] Saved config" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Config] Error saving config: " << e.what() << std::endl;
    }
}

void Config::saveDefault() {
    fs::path path = configPath();

    json j = {
        {"plex_url", "http://localhost:32400"},
        {"plex_token", "YOUR_PLEX_TOKEN_HERE"},
        {"polling_interval_secs", 15},
        {"start_at_boot", false}
    };

    try {
        std::ofstream file(path);
        file << j.dump(4);
        std::cout << "[Config] Created default config at: " << path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Config] Error saving config: " << e.what() << std::endl;
    }
}

#ifdef _WIN32
static const wchar_t* STARTUP_REG_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* STARTUP_VALUE_NAME = L"Pleyx";

bool Config::isStartupEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type;
    DWORD size = 0;
    bool exists = RegQueryValueExW(hKey, STARTUP_VALUE_NAME, nullptr, &type, nullptr, &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return exists;
}

void Config::setStartupEnabled(bool enabled) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        std::cerr << "[Config] Failed to open registry key" << std::endl;
        return;
    }

    if (enabled) {
        // Get current executable path
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        if (RegSetValueExW(hKey, STARTUP_VALUE_NAME, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(exePath),
            static_cast<DWORD>((wcslen(exePath) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS) {
            std::cout << "[Config] Added to startup" << std::endl;
        }
    } else {
        RegDeleteValueW(hKey, STARTUP_VALUE_NAME);
        std::cout << "[Config] Removed from startup" << std::endl;
    }

    RegCloseKey(hKey);
}
#else
bool Config::isStartupEnabled() { return false; }
void Config::setStartupEnabled(bool enabled) {}
#endif
