
#pragma once

#include <string>
#include <mysql.h>

class DatabaseService
{
public:
    static DatabaseService& Instance();

    bool Connect(
        const char* host,
        const char* username,
        const char* password,
        const char* database,
        unsigned int port = 3306);

    void Disconnect();
    bool IsConnected() const;
    std::string GetLastError() const;

    // Register a user; password is securely hashed before storage.
    bool RegisterUser(
        const std::string& username,
        const std::string& email,
        const std::string& password,
        const std::string& displayName);

    // Authenticate using username or email.
    bool AuthenticateUser(
        const std::string& usernameOrEmail,
        const std::string& password,
        long long& userId,
        std::string& displayName);

    // Server-side TCP authentication.
    bool AuthenticateForChat(
        const std::string& usernameOrEmail,
        const std::string& password,
        long long& userId,
        std::string& displayName);

    // Save PRIVATE or GROUP messages in the same table.
    // recipientId is ignored for GROUP messages.
    bool SaveMessage(
        long long conversationId,
        long long senderId,
        long long recipientId,
        const std::string& messageType,
        const std::string& messageText);

    // Update users.is_online.
    bool SetUserOnline(
        long long userId,
        bool online);

private:
    DatabaseService() = default;
    ~DatabaseService();

    DatabaseService(const DatabaseService&) = delete;
    DatabaseService& operator=(const DatabaseService&) = delete;

    MYSQL* m_connection = nullptr;
    std::string m_lastError;
};
