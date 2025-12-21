#include "plex.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

using json = nlohmann::json;

std::string NowPlaying::displayTitle() const {
    switch (mediaType) {
        case MediaType::Episode:
            if (grandparentTitle) {
                return *grandparentTitle + " - " + title;
            }
            return title;
        case MediaType::Track:
            if (grandparentTitle) {
                return *grandparentTitle + " - " + title;
            }
            return title;
        default:
            if (year) {
                return title + " (" + std::to_string(*year) + ")";
            }
            return title;
    }
}

std::string NowPlaying::stateText() const {
    switch (playerState) {
        case PlayerState::Playing: return "Playing";
        case PlayerState::Paused: return "Paused";
        case PlayerState::Buffering: return "Buffering";
        default: return "Stopped";
    }
}

PlexClient::PlexClient(const std::string& serverUrl, const std::string& token)
    : serverUrl(serverUrl), token(token) {
    // Remove trailing slash
    while (!this->serverUrl.empty() && this->serverUrl.back() == '/') {
        this->serverUrl.pop_back();
    }
}

#ifdef _WIN32
std::string PlexClient::httpGet(const std::string& path) {
    std::string result;

    // Parse URL
    std::string fullUrl = serverUrl + path;
    std::wstring wUrl(fullUrl.begin(), fullUrl.end());

    URL_COMPONENTS urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = {0};
    wchar_t urlPath[2048] = {0};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 2048;

    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &urlComp)) {
        std::cerr << "[Plex] Failed to parse URL" << std::endl;
        return "";
    }

    HINTERNET hSession = WinHttpOpen(L"Pleyx/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) {
        std::cerr << "[Plex] Failed to open session" << std::endl;
        return "";
    }

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        std::cerr << "[Plex] Failed to connect" << std::endl;
        return "";
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        std::cerr << "[Plex] Failed to open request" << std::endl;
        return "";
    }

    // Add headers
    std::wstring tokenHeader = L"X-Plex-Token: " + std::wstring(token.begin(), token.end());
    WinHttpAddRequestHeaders(hRequest, tokenHeader.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, L"Accept: application/json", -1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        std::cerr << "[Plex] Failed to send request" << std::endl;
        return "";
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        std::cerr << "[Plex] Failed to receive response" << std::endl;
        return "";
    }

    // Read response
    DWORD bytesAvailable = 0;
    DWORD bytesRead = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable + 1);
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            result.append(buffer.data(), bytesRead);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}
#else
std::string PlexClient::httpGet(const std::string& path) {
    return "";
}
#endif

bool PlexClient::testConnection() {
    std::string response = httpGet("/");
    return !response.empty();
}

std::string PlexClient::extractImdbId(const std::string& jsonResponse, const std::string& ratingKey) {
    try {
        auto j = json::parse(jsonResponse);
        auto& metadata = j["MediaContainer"]["Metadata"];

        for (auto& item : metadata) {
            if (item.contains("Guid")) {
                for (auto& guid : item["Guid"]) {
                    std::string id = guid.value("id", "");
                    if (id.find("imdb://") == 0) {
                        return id.substr(7);  // Remove "imdb://" prefix
                    }
                }
            }
        }
    } catch (...) {}

    return "";
}

std::optional<NowPlaying> PlexClient::getNowPlaying() {
    std::string response = httpGet("/status/sessions");
    if (response.empty()) {
        return std::nullopt;
    }

    try {
        auto j = json::parse(response);

        if (!j.contains("MediaContainer") || !j["MediaContainer"].contains("Metadata")) {
            return std::nullopt;
        }

        auto& metadataArray = j["MediaContainer"]["Metadata"];
        if (metadataArray.empty()) {
            return std::nullopt;
        }

        // Find the best session: prefer "playing" over others, take last in array
        const json* bestSession = nullptr;
        for (auto it = metadataArray.rbegin(); it != metadataArray.rend(); ++it) {
            auto& item = *it;
            if (item.contains("Player")) {
                std::string state = item["Player"].value("state", "");
                if (state == "playing") {
                    bestSession = &item;
                    break;
                }
            }
            if (!bestSession) {
                bestSession = &item;
            }
        }

        if (!bestSession) {
            return std::nullopt;
        }

        auto& item = *bestSession;
        NowPlaying np;

        // Title
        np.title = item.value("title", "Unknown");

        // Media type
        std::string type = item.value("type", "");
        if (type == "movie") np.mediaType = MediaType::Movie;
        else if (type == "episode") np.mediaType = MediaType::Episode;
        else if (type == "track") np.mediaType = MediaType::Track;

        // Player state
        if (item.contains("Player")) {
            std::string state = item["Player"].value("state", "");
            if (state == "playing") np.playerState = PlayerState::Playing;
            else if (state == "paused") np.playerState = PlayerState::Paused;
            else if (state == "buffering") np.playerState = PlayerState::Buffering;
        }

        // Optional fields
        if (item.contains("year")) np.year = item["year"].get<int>();
        if (item.contains("grandparentTitle")) np.grandparentTitle = item["grandparentTitle"].get<std::string>();
        if (item.contains("parentTitle")) np.parentTitle = item["parentTitle"].get<std::string>();
        if (item.contains("parentIndex")) np.seasonNumber = item["parentIndex"].get<int>();
        if (item.contains("index")) np.episodeNumber = item["index"].get<int>();

        // Duration and progress
        np.durationMs = item.value("duration", 0);
        np.progressMs = item.value("viewOffset", 0);

        // Extract genres
        if (item.contains("Genre")) {
            for (auto& genre : item["Genre"]) {
                if (genre.contains("tag")) {
                    np.genres.push_back(genre["tag"].get<std::string>());
                }
            }
        }

        // Extract IMDB ID from Guid array
        if (item.contains("Guid")) {
            for (auto& guid : item["Guid"]) {
                std::string id = guid.value("id", "");
                if (id.find("imdb://") == 0) {
                    np.imdbId = id.substr(7);
                    break;
                }
            }
        }

        // Extract artwork URL - use grandparentArt for shows, art/thumb for movies
        std::string artPath;
        if (np.mediaType == MediaType::Episode && item.contains("grandparentArt")) {
            artPath = item["grandparentArt"].get<std::string>();
        } else if (np.mediaType == MediaType::Track) {
            // For music, prefer parentThumb (album art)
            if (item.contains("parentThumb")) {
                artPath = item["parentThumb"].get<std::string>();
            } else if (item.contains("grandparentThumb")) {
                artPath = item["grandparentThumb"].get<std::string>();
            }
        } else if (item.contains("art")) {
            artPath = item["art"].get<std::string>();
        } else if (item.contains("thumb")) {
            artPath = item["thumb"].get<std::string>();
        }

        if (!artPath.empty()) {
            np.artPath = artPath;
        }

        std::cout << "[Plex] Now playing: " << np.displayTitle()
                  << " (" << np.stateText() << ")"
                  << " IMDB: " << np.imdbId.value_or("none")
                  << " Art: " << (np.artPath ? "yes" : "no") << std::endl;

        return np;

    } catch (const std::exception& e) {
        std::cerr << "[Plex] JSON parse error: " << e.what() << std::endl;
        return std::nullopt;
    }
}
