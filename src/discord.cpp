#include "discord.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>

using json = nlohmann::json;

Discord::Discord(const std::string& clientId) : clientId(clientId) {}

Discord::~Discord() {
    disconnect();
}

bool Discord::connect() {
    if (!ipc.openPipe()) {
        return false;
    }

    if (!ipc.sendHandshake(clientId)) {
        ipc.closePipe();
        return false;
    }

    handshakeDone = true;
    return true;
}

void Discord::disconnect() {
    if (handshakeDone) {
        clearPresence();
    }
    ipc.closePipe();
    handshakeDone = false;
}

bool Discord::isConnected() const {
    return ipc.isConnected() && handshakeDone;
}

std::string Discord::buildActivityJson(const MediaInfo& info) {
    json activity;

    // Activity type (0=Playing, 2=Listening, 3=Watching)
    activity["type"] = static_cast<int>(info.activityType);

    // Details and state
    activity["details"] = info.details;
    activity["state"] = info.state;

    // Assets
    activity["assets"] = {
        {"large_image", info.largeImage},
        {"large_text", info.largeText},
        {"small_image", info.smallImage},
        {"small_text", info.smallText}
    };

    // Timestamps for progress bar
    if (info.isPlaying && info.durationMs > 0) {
        auto now = std::chrono::system_clock::now();
        auto epoch = now.time_since_epoch();
        int64_t nowSecs = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

        int64_t startTime = nowSecs - (info.progressMs / 1000);
        int64_t endTime = nowSecs + ((info.durationMs - info.progressMs) / 1000);

        activity["timestamps"] = {
            {"start", startTime},
            {"end", endTime}
        };
    }

    // Buttons
    json buttons = json::array();
    if (info.imdbId.has_value() && !info.imdbId->empty()) {
        buttons.push_back({
            {"label", "View on IMDb"},
            {"url", "https://www.imdb.com/title/" + *info.imdbId}
        });
    }

    if (!buttons.empty()) {
        activity["buttons"] = buttons;
    }

    return activity.dump();
}

bool Discord::updatePresence(const MediaInfo& info) {
    if (!isConnected()) {
        if (!connect()) {
            return false;
        }
    }

    std::string activityJson = buildActivityJson(info);
    return ipc.sendActivity(activityJson);
}

bool Discord::clearPresence() {
    if (!isConnected()) {
        return false;
    }
    return ipc.clearActivity();
}
