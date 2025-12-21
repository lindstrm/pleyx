#include "config.h"
#include "plex.h"
#include "discord.h"
#include "image_cache.h"
#include "tray_icon.h"
#include "resource.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_OPEN_CONFIG 1002
#define ID_TRAY_START_AT_BOOT 1003

NOTIFYICONDATAW nid = {0};
HMENU hMenu = nullptr;
std::atomic<bool> running{true};
TrayIcon* g_trayIcon = nullptr;
std::atomic<bool> g_isPlaying{false};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            // With NOTIFYICON_VERSION_4, the actual message is in LOWORD(lParam)
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                PostMessage(hwnd, WM_NULL, 0, 0);  // Required to dismiss menu properly
            }
            break;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_EXIT:
                    running = false;
                    PostQuitMessage(0);
                    break;
                case ID_TRAY_OPEN_CONFIG:
                    ShellExecuteW(nullptr, L"open", Config::configPath().c_str(), nullptr, nullptr, SW_SHOW);
                    break;
                case ID_TRAY_START_AT_BOOT: {
                    bool currentlyEnabled = Config::isStartupEnabled();
                    Config::setStartupEnabled(!currentlyEnabled);
                    // Update menu checkmark
                    CheckMenuItem(hMenu, ID_TRAY_START_AT_BOOT,
                        MF_BYCOMMAND | (!currentlyEnabled ? MF_CHECKED : MF_UNCHECKED));
                    break;
                }
            }
            break;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

std::wstring getIconPath() {
    // Try exe directory first
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir(exePath);
    size_t pos = exeDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        exeDir = exeDir.substr(0, pos);
    }

    std::wstring iconPath = exeDir + L"\\images\\plex.png";
    if (GetFileAttributesW(iconPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return iconPath;
    }

    // Try parent directory (for development)
    iconPath = exeDir + L"\\..\\images\\plex.png";
    if (GetFileAttributesW(iconPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return iconPath;
    }

    // Try two levels up (for build/Release directory)
    iconPath = exeDir + L"\\..\\..\\images\\plex.png";
    if (GetFileAttributesW(iconPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return iconPath;
    }

    return L"";
}

void setupTray(HWND hwnd, HINSTANCE hInstance) {
    // Create menu
    hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_CONFIG, L"Open Config");
    AppendMenuW(hMenu, MF_STRING | (Config::isStartupEnabled() ? MF_CHECKED : 0),
        ID_TRAY_START_AT_BOOT, L"Start at Boot");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Quit");

    // Load custom icon - try embedded ICO resource first, then file
    g_trayIcon = new TrayIcon();
    bool iconLoaded = g_trayIcon->loadFromIconResource(hInstance, IDI_ICON1);

    if (!iconLoaded) {
        // Fall back to PNG file
        std::wstring iconPath = getIconPath();
        if (!iconPath.empty()) {
            iconLoaded = g_trayIcon->load(iconPath);
        }
    }

    if (!iconLoaded) {
        delete g_trayIcon;
        g_trayIcon = nullptr;
    }

    // Setup tray icon
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAYICON;

    // Use custom gray icon if available, otherwise default
    if (g_trayIcon && g_trayIcon->getGrayIcon()) {
        nid.hIcon = g_trayIcon->getGrayIcon();
    } else {
        nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION
    }

    wcscpy_s(nid.szTip, L"Pleyx - Plex Discord Presence");
    nid.uVersion = NOTIFYICON_VERSION_4;

    BOOL result = Shell_NotifyIconW(NIM_ADD, &nid);
    if (result) {
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        std::cout << "[Tray] Icon added successfully" << std::endl;
    } else {
        std::cout << "[Tray] Failed to add icon, error: " << GetLastError() << std::endl;
    }
}

void updateTrayTip(const std::wstring& tip) {
    // Tooltip max is 128 chars including null terminator
    std::wstring truncated = tip.substr(0, 127);
    wcscpy_s(nid.szTip, truncated.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void setTrayIconPlaying(bool playing) {
    if (!g_trayIcon) return;

    bool wasPlaying = g_isPlaying.exchange(playing);
    if (wasPlaying == playing) return;  // No change

    nid.hIcon = playing ? g_trayIcon->getColorIcon() : g_trayIcon->getGrayIcon();
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
#endif

const char* DISCORD_CLIENT_ID = "1451961488427188355";

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Load config first to check debug setting
    Config config = Config::load();

    // Allocate console only if debug is enabled
    if (config.debug) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        std::cout << "=== Pleyx Starting ===" << std::endl;
    }

    if (config.plexToken.empty() || config.plexToken == "YOUR_PLEX_TOKEN_HERE") {
        MessageBoxW(nullptr,
            L"Please configure your Plex token in the config file.\n\nThe config file will now open.",
            L"Pleyx - Configuration Required",
            MB_ICONWARNING | MB_OK);
        ShellExecuteW(nullptr, L"open", Config::configPath().c_str(), nullptr, nullptr, SW_SHOW);
        return 1;
    }

    // Test Plex connection
    PlexClient plex(config.plexUrl, config.plexToken);
    if (!plex.testConnection()) {
        MessageBoxW(nullptr,
            L"Failed to connect to Plex server.\n\nPlease check your configuration.",
            L"Pleyx - Connection Error",
            MB_ICONERROR | MB_OK);
        return 1;
    }

    std::cout << "[Plex] Connected to server" << std::endl;

    // Set OMDB API key if configured
    if (!config.omdbApiKey.empty()) {
        setOmdbApiKey(config.omdbApiKey);
    }

    // Create Discord client
    Discord discord(DISCORD_CLIENT_ID);

    // Create image cache for uploading artwork to catbox
    ImageCache imageCache(config.plexUrl, config.plexToken);

    // Create hidden window for tray
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"PleyxTray";

    if (!RegisterClassW(&wc)) {
        std::cout << "[Tray] Failed to register window class: " << GetLastError() << std::endl;
    }

    HWND hwnd = CreateWindowExW(0, L"PleyxTray", L"Pleyx", 0,
        0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        std::cout << "[Tray] Failed to create window: " << GetLastError() << std::endl;
    } else {
        std::cout << "[Tray] Window created: " << hwnd << std::endl;
    }

    setupTray(hwnd, hInstance);

    // Start polling thread
    std::thread pollThread([&]() {
        std::string lastKey;

        while (running) {
            try {
                auto nowPlaying = plex.getNowPlaying();

            if (nowPlaying.has_value()) {
                auto& np = *nowPlaying;

                // Create a key to detect changes
                std::string currentKey = np.title + std::to_string(static_cast<int>(np.playerState))
                    + std::to_string(np.progressMs / 10000);  // Update every 10 seconds

                if (currentKey != lastKey) {
                    lastKey = currentKey;

                    // Update tray tooltip (safely convert to wide string)
                    try {
                        std::string title = np.displayTitle();
                        if (title.length() > 100) title = title.substr(0, 100) + "...";
                        std::wstring tip = L"Pleyx - ";
                        for (char c : title) {
                            tip += static_cast<wchar_t>(static_cast<unsigned char>(c));
                        }
                        updateTrayTip(tip);
                    } catch (...) {
                        updateTrayTip(L"Pleyx - Now playing");
                    }
                }

                // Only show presence when playing
                if (np.playerState == PlayerState::Playing) {
                    setTrayIconPlaying(true);
                    MediaInfo info;
                    info.details = np.displayTitle();
                    info.isPlaying = true;
                    info.durationMs = np.durationMs;
                    info.progressMs = np.progressMs;
                    info.imdbId = np.imdbId;

                    // Get artwork URL - prefer OMDB poster, fall back to catbox
                    std::string artUrl;
                    if (np.posterUrl) {
                        artUrl = *np.posterUrl;
                    } else if (np.artPath) {
                        artUrl = imageCache.getCatboxUrl(*np.artPath);
                    }

                    // Set state and activity type based on media type
                    switch (np.mediaType) {
                        case MediaType::Episode:
                            info.activityType = ActivityType::Watching;
                            info.details = np.grandparentTitle.value_or("TV Show");
                            info.largeImage = artUrl.empty() ? "tv" : artUrl;
                            info.largeText = np.grandparentTitle.value_or("Watching TV");
                            if (np.seasonNumber && np.episodeNumber) {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "S%02dE%02d • %s",
                                    *np.seasonNumber, *np.episodeNumber, np.title.c_str());
                                info.state = buf;
                            } else {
                                info.state = np.title;
                            }
                            break;
                        case MediaType::Movie: {
                            info.activityType = ActivityType::Watching;
                            info.largeImage = artUrl.empty() ? "movie" : artUrl;
                            info.largeText = np.title;
                            // Build state: ratings • genres
                            std::string stateStr;
                            if (np.imdbRating) {
                                stateStr = *np.imdbRating;
                            }
                            if (np.rottenTomatoesRating) {
                                if (!stateStr.empty()) stateStr += " • ";
                                stateStr += *np.rottenTomatoesRating;
                            }
                            if (!np.genres.empty()) {
                                if (!stateStr.empty()) stateStr += " • ";
                                for (size_t i = 0; i < np.genres.size(); i++) {
                                    if (i > 0) stateStr += ", ";
                                    stateStr += np.genres[i];
                                }
                            }
                            info.state = stateStr.empty() ? np.stateText() : stateStr;
                            break;
                        }
                        case MediaType::Track: {
                            info.activityType = ActivityType::Listening;
                            info.details = np.title;
                            info.largeImage = artUrl.empty() ? "music" : artUrl;
                            std::string artist = np.grandparentTitle.value_or("Unknown Artist");
                            std::string album = np.parentTitle.value_or("Unknown Album");
                            info.largeText = artist + " - " + album;
                            if (!np.genres.empty()) {
                                info.state = np.genres[0];
                            } else {
                                info.state = "Music";
                            }
                            break;
                        }
                        default:
                            info.activityType = ActivityType::Playing;
                            info.largeImage = "plex";
                            info.largeText = "Plex";
                            info.state = np.stateText();
                    }

                    discord.updatePresence(info);
                } else {
                    setTrayIconPlaying(false);
                    discord.clearPresence();
                }
            } else {
                if (!lastKey.empty()) {
                    lastKey.clear();
                    setTrayIconPlaying(false);
                    updateTrayTip(L"Pleyx - Nothing playing");
                    discord.clearPresence();
                }
            }

            } catch (const std::exception& e) {
                std::cerr << "[Error] Exception in poll loop: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[Error] Unknown exception in poll loop" << std::endl;
            }

            // Sleep in 1-second intervals for faster shutdown
            for (int i = 0; i < config.pollingIntervalSecs && running; i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }

        discord.disconnect();
    });

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    running = false;
    pollThread.join();

    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (hMenu) DestroyMenu(hMenu);
    if (g_trayIcon) {
        delete g_trayIcon;
        g_trayIcon = nullptr;
    }

    std::cout << "=== Pleyx Stopped ===" << std::endl;
    return 0;
}
