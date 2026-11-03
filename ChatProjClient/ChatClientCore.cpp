
#include "pch.h"
#include "framework.h"
#include "ChatClientCore.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <utility>

#pragma comment(lib, "Ws2_32.lib")

namespace ChatClientCore
{
    static SOCKET g_socket = INVALID_SOCKET;
    static std::mutex g_socketMutex;
    static std::mutex g_sendMutex;
    static std::mutex g_callbackMutex;
    static std::mutex g_credentialsMutex;

    static MessageCallback g_callback;
    static std::thread g_receiver;

    static std::string g_username;
    static std::string g_password;
    static std::string g_displayName;

    static std::atomic<int> g_clientId{ 0 };
    static std::atomic<long long> g_userId{ 0 };
    static std::atomic<bool> g_running{ false };
    static std::atomic<bool> g_authenticated{ false };

    static bool g_winsockStarted = false;

    static std::string Encode(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());

        for (char c : s)
        {
            if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else out += c;
        }

        return out;
    }

    static bool Decode(const std::string& s, std::string& out)
    {
        out.clear();

        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] != '\\')
            {
                out += s[i];
                continue;
            }

            if (++i >= s.size())
                return false;

            if (s[i] == '\\') out += '\\';
            else if (s[i] == 'n') out += '\n';
            else if (s[i] == 'r') out += '\r';
            else return false;
        }

        return true;
    }

    static bool SendFrame(const std::string& message)
    {
        if (!g_running || g_socket == INVALID_SOCKET)
            return false;

        const std::string frame = Encode(message) + "\n";
        size_t offset = 0;

        while (offset < frame.size())
        {
            int n = send(
                g_socket,
                frame.data() + offset,
                static_cast<int>(frame.size() - offset),
                0);

            if (n == SOCKET_ERROR || n == 0)
                return false;

            offset += static_cast<size_t>(n);
        }

        return true;
    }

    void SetLoginCredentials(
        const std::string& usernameOrEmail,
        const std::string& password)
    {
        std::lock_guard<std::mutex> lock(g_credentialsMutex);
        g_username = usernameOrEmail;
        g_password = password;
    }

    void SetMessageCallback(MessageCallback callback)
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        g_callback = std::move(callback);
    }

    int GetClientId()
    {
        return g_clientId.load();
    }

    long long GetUserId()
    {
        return g_userId.load();
    }

    bool IsAuthenticated()
    {
        return g_authenticated.load();
    }

    bool Connect(const char* host, unsigned short port)
    {
        std::lock_guard<std::mutex> socketLock(g_socketMutex);

        if (g_socket != INVALID_SOCKET && g_running)
            return true;

        if (g_receiver.joinable())
        {
            g_running = false;

            if (g_socket != INVALID_SOCKET)
            {
                shutdown(g_socket, SD_BOTH);
                closesocket(g_socket);
                g_socket = INVALID_SOCKET;
            }

            g_receiver.join();
        }

        std::string username;
        std::string password;

        {
            std::lock_guard<std::mutex> lock(g_credentialsMutex);
            username = g_username;
            password = g_password;
        }

        if (username.empty() || password.empty())
            return false;

        // This simple wire format uses '|' as a field separator.
        // Reject these credentials rather than sending an ambiguous frame.
        if (username.find('|') != std::string::npos ||
            password.find('|') != std::string::npos)
            return false;

        WSADATA data{};

        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            return false;

        g_winsockStarted = true;

        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (s == INVALID_SOCKET)
        {
            WSACleanup();
            g_winsockStarted = false;
            return false;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);

        if (inet_pton(AF_INET, host, &address.sin_addr) != 1 ||
            connect(
                s,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) == SOCKET_ERROR)
        {
            closesocket(s);
            WSACleanup();
            g_winsockStarted = false;
            return false;
        }

        g_socket = s;
        g_running = true;
        g_authenticated = false;
        g_clientId = 0;
        g_userId = 0;
        g_displayName.clear();

        // Send authentication before allowing normal chat messages.
        {
            std::lock_guard<std::mutex> sendLock(g_sendMutex);

            if (!SendFrame("AUTH|" + username + "|" + password))
            {
                g_running = false;
                closesocket(g_socket);
                g_socket = INVALID_SOCKET;
                WSACleanup();
                g_winsockStarted = false;
                return false;
            }
        }

        g_receiver = std::thread([s]()
            {
                char buffer[2048];
                std::string pending;

                while (g_running)
                {
                    int n = recv(s, buffer, sizeof(buffer), 0);

                    if (n <= 0)
                        break;

                    pending.append(buffer, static_cast<size_t>(n));

                    if (pending.size() > 65536)
                        break;

                    size_t pos = 0;

                    while ((pos = pending.find('\n')) != std::string::npos)
                    {
                        std::string encoded = pending.substr(0, pos);
                        pending.erase(0, pos + 1);

                        std::string message;

                        if (!Decode(encoded, message))
                        {
                            g_running = false;
                            break;
                        }

                        if (message.rfind("WELCOME|", 0) == 0)
                        {
                            // Expected: WELCOME|clientId|userId|displayName
                            size_t p1 = message.find('|', 8);
                            size_t p2 = (p1 == std::string::npos)
                                ? std::string::npos
                                : message.find('|', p1 + 1);

                            if (p1 != std::string::npos &&
                                p2 != std::string::npos)
                            {
                                try
                                {
                                    g_clientId = std::stoi(
                                        message.substr(8, p1 - 8));

                                    g_userId = std::stoll(
                                        message.substr(p1 + 1, p2 - p1 - 1));

                                    g_displayName = message.substr(p2 + 1);

                                    g_authenticated =
                                        (g_clientId > 0 && g_userId > 0);
                                }
                                catch (...)
                                {
                                    g_clientId = 0;
                                    g_userId = 0;
                                    g_authenticated = false;
                                }
                            }
                        }
                        else if (message.rfind("AUTH_ERROR|", 0) == 0)
                        {
                            g_authenticated = false;
                        }

                        MessageCallback callback;

                        {
                            std::lock_guard<std::mutex> lock(g_callbackMutex);
                            callback = g_callback;
                        }

                        if (callback)
                            callback(message);
                    }
                }

                g_authenticated = false;
                g_running = false;
            });

        return true;
    }

    bool SendChatMessage(const std::string& message)
    {
        std::lock_guard<std::mutex> sendLock(g_sendMutex);

        if (!g_running || !g_authenticated)
            return false;

        if (g_socket == INVALID_SOCKET)
            return false;

        if (!SendFrame(message))
        {
            g_running = false;
            g_authenticated = false;
            return false;
        }

        return true;
    }

    void Disconnect()
    {
        std::lock_guard<std::mutex> socketLock(g_socketMutex);

        g_running = false;
        g_authenticated = false;

        SOCKET s = g_socket;
        g_socket = INVALID_SOCKET;

        if (s != INVALID_SOCKET)
        {
            shutdown(s, SD_BOTH);
            closesocket(s);
        }

        if (g_receiver.joinable())
            g_receiver.join();

        if (g_winsockStarted)
        {
            WSACleanup();
            g_winsockStarted = false;
        }

        g_clientId = 0;
        g_userId = 0;
        g_displayName.clear();
    }
}
