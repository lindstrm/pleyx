#include "image_cache.h"
#include <iostream>
#include <sstream>
#include <random>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

ImageCache::ImageCache(const std::string& plexUrl, const std::string& plexToken)
    : plexUrl(plexUrl), plexToken(plexToken) {
    // Remove trailing slash from plex URL
    while (!this->plexUrl.empty() && this->plexUrl.back() == '/') {
        this->plexUrl.pop_back();
    }
}

std::string ImageCache::getCatboxUrl(const std::string& artPath) {
    if (artPath.empty()) {
        return "";
    }

    std::lock_guard<std::mutex> lock(cacheMutex);

    // Check cache first
    auto it = cache.find(artPath);
    if (it != cache.end()) {
        return it->second;
    }

    // Download from Plex
    std::cout << "[ImageCache] Downloading: " << artPath << std::endl;
    auto imageData = downloadFromPlex(artPath);
    if (imageData.empty()) {
        std::cerr << "[ImageCache] Failed to download image" << std::endl;
        return "";
    }

    std::cout << "[ImageCache] Downloaded " << imageData.size() << " bytes, uploading to catbox..." << std::endl;

    // Upload to catbox
    std::string catboxUrl = uploadToCatbox(imageData);
    if (catboxUrl.empty()) {
        std::cerr << "[ImageCache] Failed to upload to catbox" << std::endl;
        return "";
    }

    std::cout << "[ImageCache] Uploaded: " << catboxUrl << std::endl;

    // Cache the result
    cache[artPath] = catboxUrl;
    return catboxUrl;
}

#ifdef _WIN32
std::vector<uint8_t> ImageCache::downloadFromPlex(const std::string& artPath) {
    std::vector<uint8_t> result;

    std::string fullUrl = plexUrl + artPath + "?X-Plex-Token=" + plexToken;
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
        return result;
    }

    HINTERNET hSession = WinHttpOpen(L"Pleyx/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD bytesAvailable = 0;
    DWORD bytesRead = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<uint8_t> buffer(bytesAvailable);
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            result.insert(result.end(), buffer.begin(), buffer.begin() + bytesRead);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return result;
}

std::string ImageCache::uploadToCatbox(const std::vector<uint8_t>& imageData) {
    std::string result;

    // Generate boundary
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string boundary = "----WebKitFormBoundary";
    for (int i = 0; i < 16; i++) {
        boundary += hex[dis(gen)];
    }

    // Build multipart form data
    std::vector<uint8_t> body;

    // reqtype field
    std::string part1 = "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"reqtype\"\r\n\r\n"
        "fileupload\r\n";
    body.insert(body.end(), part1.begin(), part1.end());

    // file field
    std::string part2 = "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"fileToUpload\"; filename=\"image.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    body.insert(body.end(), part2.begin(), part2.end());
    body.insert(body.end(), imageData.begin(), imageData.end());

    std::string part3 = "\r\n--" + boundary + "--\r\n";
    body.insert(body.end(), part3.begin(), part3.end());

    // Connect to catbox.moe
    HINTERNET hSession = WinHttpOpen(L"Pleyx/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession, L"catbox.moe", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/user/api.php",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Set content type header
    std::wstring contentType = L"Content-Type: multipart/form-data; boundary=" +
        std::wstring(boundary.begin(), boundary.end());
    WinHttpAddRequestHeaders(hRequest, contentType.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
        std::cerr << "[ImageCache] Catbox send failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        std::cerr << "[ImageCache] Catbox receive failed: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
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

    // Trim whitespace
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }

    return result;
}
#else
std::vector<uint8_t> ImageCache::downloadFromPlex(const std::string& artPath) {
    return {};
}

std::string ImageCache::uploadToCatbox(const std::vector<uint8_t>& imageData) {
    return "";
}
#endif
