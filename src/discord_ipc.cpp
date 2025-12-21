#include "discord_ipc.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <iostream>

using json = nlohmann::json;

DiscordIPC::DiscordIPC() = default;

DiscordIPC::~DiscordIPC() {
    closePipe();
}

#ifdef _WIN32

bool DiscordIPC::openPipe() {
    if (connected) return true;

    for (int i = 0; i < 10; i++) {
        std::string pipeName = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);

        pipeHandle = CreateFileA(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr
        );

        if (pipeHandle != INVALID_HANDLE_VALUE) {
            connected = true;
            std::cout << "[Discord] Connected to pipe: " << pipeName << std::endl;
            return true;
        }
    }

    std::cerr << "[Discord] Failed to connect to any Discord pipe" << std::endl;
    return false;
}

void DiscordIPC::closePipe() {
    if (pipeHandle != INVALID_HANDLE_VALUE) {
        CancelIo(pipeHandle);
        CloseHandle(pipeHandle);
        pipeHandle = INVALID_HANDLE_VALUE;
    }
    connected = false;
}

bool DiscordIPC::writeFrame(int opcode, const std::string& payload) {
    if (!connected || pipeHandle == INVALID_HANDLE_VALUE) return false;

    try {
        // Frame format: [opcode:4 bytes][length:4 bytes][payload]
        uint32_t len = static_cast<uint32_t>(payload.size());
        std::vector<char> buffer(8 + len);
        memcpy(buffer.data(), &opcode, 4);
        memcpy(buffer.data() + 4, &len, 4);
        if (len > 0) {
            memcpy(buffer.data() + 8, payload.c_str(), len);
        }

        OVERLAPPED overlapped = {0};
        overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) {
            return false;
        }

        DWORD written = 0;
        BOOL result = WriteFile(pipeHandle, buffer.data(), static_cast<DWORD>(buffer.size()), &written, &overlapped);

        if (!result && GetLastError() == ERROR_IO_PENDING) {
            // Wait up to 5 seconds
            DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 5000);
            if (waitResult == WAIT_TIMEOUT) {
                CancelIo(pipeHandle);
                CloseHandle(overlapped.hEvent);
                closePipe();
                std::cerr << "[Discord] Write timeout" << std::endl;
                return false;
            }
            GetOverlappedResult(pipeHandle, &overlapped, &written, FALSE);
        }

        CloseHandle(overlapped.hEvent);

        if (written != buffer.size()) {
            closePipe();
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Discord] Write error: " << e.what() << std::endl;
        closePipe();
        return false;
    }
}

bool DiscordIPC::readFrame(int& opcode, std::string& data) {
    if (!connected || pipeHandle == INVALID_HANDLE_VALUE) return false;

    try {
        OVERLAPPED overlapped = {0};
        overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) {
            return false;
        }

        char header[8];
        DWORD bytesRead = 0;
        BOOL result = ReadFile(pipeHandle, header, 8, &bytesRead, &overlapped);

        if (!result && GetLastError() == ERROR_IO_PENDING) {
            // Wait up to 5 seconds
            DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 5000);
            if (waitResult == WAIT_TIMEOUT) {
                CancelIo(pipeHandle);
                CloseHandle(overlapped.hEvent);
                closePipe();
                std::cerr << "[Discord] Read timeout (header)" << std::endl;
                return false;
            }
            GetOverlappedResult(pipeHandle, &overlapped, &bytesRead, FALSE);
        }

        if (bytesRead != 8) {
            CloseHandle(overlapped.hEvent);
            closePipe();
            return false;
        }

        memcpy(&opcode, header, 4);
        uint32_t len;
        memcpy(&len, header + 4, 4);

        // Sanity check on length
        if (len > 1024 * 1024) {  // Max 1MB
            CloseHandle(overlapped.hEvent);
            closePipe();
            std::cerr << "[Discord] Invalid payload length: " << len << std::endl;
            return false;
        }

        if (len > 0) {
            data.resize(len);
            ResetEvent(overlapped.hEvent);

            result = ReadFile(pipeHandle, &data[0], len, &bytesRead, &overlapped);
            if (!result && GetLastError() == ERROR_IO_PENDING) {
                DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 5000);
                if (waitResult == WAIT_TIMEOUT) {
                    CancelIo(pipeHandle);
                    CloseHandle(overlapped.hEvent);
                    closePipe();
                    std::cerr << "[Discord] Read timeout (payload)" << std::endl;
                    return false;
                }
                GetOverlappedResult(pipeHandle, &overlapped, &bytesRead, FALSE);
            }

            if (bytesRead != len) {
                CloseHandle(overlapped.hEvent);
                closePipe();
                return false;
            }
        } else {
            data.clear();
        }

        CloseHandle(overlapped.hEvent);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Discord] Read error: " << e.what() << std::endl;
        closePipe();
        return false;
    }
}

#else
// Unix implementation would go here
bool DiscordIPC::openPipe() { return false; }
void DiscordIPC::closePipe() {}
bool DiscordIPC::writeFrame(int, const std::string&) { return false; }
bool DiscordIPC::readFrame(int&, std::string&) { return false; }
#endif

bool DiscordIPC::isConnected() const {
    return connected;
}

bool DiscordIPC::sendHandshake(const std::string& clientId) {
    try {
        json handshake = {
            {"v", 1},
            {"client_id", clientId}
        };

        std::string payload = handshake.dump();
        std::cout << "[Discord] Sending handshake..." << std::endl;

        if (!writeFrame(OP_HANDSHAKE, payload)) {
            return false;
        }

        // Read response
        int opcode;
        std::string response;
        if (!readFrame(opcode, response)) {
            return false;
        }

        std::cout << "[Discord] Handshake complete" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Discord] Handshake error: " << e.what() << std::endl;
        return false;
    }
}

bool DiscordIPC::sendActivity(const std::string& activityJson) {
    try {
        nonce++;

        json activity = json::parse(activityJson);
        json payload = {
            {"cmd", "SET_ACTIVITY"},
            {"args", {
                {"pid", GetCurrentProcessId()},
                {"activity", activity}
            }},
            {"nonce", std::to_string(nonce)}
        };

        std::string payloadStr = payload.dump();

        if (!writeFrame(OP_FRAME, payloadStr)) {
            return false;
        }

        // Read response
        int opcode;
        std::string response;
        if (!readFrame(opcode, response)) {
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Discord] Activity error: " << e.what() << std::endl;
        closePipe();
        return false;
    }
}

bool DiscordIPC::clearActivity() {
    try {
        nonce++;

        json payload = {
            {"cmd", "SET_ACTIVITY"},
            {"args", {
                {"pid", GetCurrentProcessId()}
            }},
            {"nonce", std::to_string(nonce)}
        };

        if (!writeFrame(OP_FRAME, payload.dump())) {
            return false;
        }

        // Read response to prevent pipe backup
        int opcode;
        std::string response;
        readFrame(opcode, response);

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Discord] Clear error: " << e.what() << std::endl;
        return false;
    }
}
