#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

class ImageCache {
public:
    ImageCache(const std::string& plexUrl, const std::string& plexToken);

    // Get catbox URL for a Plex art path, uploading if needed
    std::string getCatboxUrl(const std::string& artPath);

private:
    std::vector<uint8_t> downloadFromPlex(const std::string& artPath);
    std::string uploadToCatbox(const std::vector<uint8_t>& imageData);

    std::string plexUrl;
    std::string plexToken;
    std::unordered_map<std::string, std::string> cache;  // artPath -> catbox URL
    std::mutex cacheMutex;
};
