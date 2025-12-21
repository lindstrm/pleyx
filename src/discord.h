#pragma once

#include "discord_ipc.h"
#include <string>
#include <optional>
#include <cstdint>

enum class ActivityType {
    Playing = 0,
    Streaming = 1,
    Listening = 2,
    Watching = 3,
    Competing = 5
};

struct MediaInfo {
    std::string title;
    std::string details;
    std::string state;
    std::string largeImage;
    std::string largeText;
    std::string smallImage = "plex";
    std::string smallText = "Plex";
    std::optional<std::string> imdbId;
    int64_t durationMs = 0;
    int64_t progressMs = 0;
    bool isPlaying = false;
    ActivityType activityType = ActivityType::Playing;
};

class Discord {
public:
    Discord(const std::string& clientId);
    ~Discord();

    bool connect();
    void disconnect();
    bool isConnected() const;

    bool updatePresence(const MediaInfo& info);
    bool clearPresence();

private:
    std::string buildActivityJson(const MediaInfo& info);

    std::string clientId;
    DiscordIPC ipc;
    bool handshakeDone = false;
};
