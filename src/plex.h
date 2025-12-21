#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>

enum class MediaType {
    Movie,
    Episode,
    Track,
    Unknown
};

enum class PlayerState {
    Playing,
    Paused,
    Buffering,
    Stopped
};

struct NowPlaying {
    std::string title;
    MediaType mediaType = MediaType::Unknown;
    PlayerState playerState = PlayerState::Stopped;
    std::optional<int> year;
    std::optional<std::string> grandparentTitle;  // Show name or artist
    std::optional<std::string> parentTitle;       // Season or album
    std::optional<int> seasonNumber;
    std::optional<int> episodeNumber;
    std::optional<std::string> imdbId;
    std::optional<std::string> posterUrl;         // Poster URL from OMDB
    std::optional<std::string> imdbRating;        // IMDB rating (e.g. "8.0/10")
    std::optional<std::string> rottenTomatoesRating; // RT rating (e.g. "85%")
    std::optional<std::string> artPath;           // Path to artwork (e.g. /library/metadata/123/art)
    std::vector<std::string> genres;
    int64_t durationMs = 0;
    int64_t progressMs = 0;

    std::string displayTitle() const;
    std::string stateText() const;
};

// Set OMDB API key for IMDB lookups
void setOmdbApiKey(const std::string& apiKey);

class PlexClient {
public:
    PlexClient(const std::string& serverUrl, const std::string& token);

    std::optional<NowPlaying> getNowPlaying();
    bool testConnection();

private:
    std::string httpGet(const std::string& path);
    std::string extractImdbId(const std::string& jsonResponse, const std::string& ratingKey);

    std::string serverUrl;
    std::string token;
};
