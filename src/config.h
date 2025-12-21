#pragma once

#include <string>
#include <filesystem>

struct Config {
    std::string plexUrl;
    std::string plexToken;
    int pollingIntervalSecs = 15;
    bool startAtBoot = false;
    bool debug = false;

    static std::filesystem::path configPath();
    static Config load();
    void save() const;
    static void saveDefault();

    // Windows startup management
    static bool isStartupEnabled();
    static void setStartupEnabled(bool enabled);
};
