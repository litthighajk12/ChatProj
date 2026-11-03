
#pragma once

#include <string>
#include <functional>

namespace ChatClientCore
{
    using MessageCallback = std::function<void(const std::string&)>;

    // Set these after the login page successfully authenticates locally.
    void SetLoginCredentials(
        const std::string& usernameOrEmail,
        const std::string& password);

    bool Connect(
        const char* host = "127.0.0.1",
        unsigned short port = 5000);

    bool SendChatMessage(const std::string& message);
    void SetMessageCallback(MessageCallback callback);

    int GetClientId();
    long long GetUserId();
    bool IsAuthenticated();

    void Disconnect();
}
