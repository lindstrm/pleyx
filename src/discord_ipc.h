#pragma once

#include <string>
#include <atomic>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

enum DiscordOpcodes {
    OP_HANDSHAKE = 0,
    OP_FRAME = 1,
    OP_CLOSE = 2,
    OP_PING = 3,
    OP_PONG = 4
};

class DiscordIPC {
public:
    DiscordIPC();
    ~DiscordIPC();

    bool openPipe();
    void closePipe();
    bool isConnected() const;

    bool writeFrame(int opcode, const std::string& payload);
    bool readFrame(int& opcode, std::string& data);
    bool sendHandshake(const std::string& clientId);
    bool sendActivity(const std::string& activityJson);
    bool clearActivity();

private:
    std::atomic<bool> connected{false};
    int nonce{0};

#ifdef _WIN32
    HANDLE pipeHandle{INVALID_HANDLE_VALUE};
#else
    int pipeFd{-1};
#endif
};
