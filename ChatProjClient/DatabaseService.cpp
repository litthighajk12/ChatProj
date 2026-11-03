#include "pch.h"
#include "framework.h"
#include "DatabaseService.h"

#include <Windows.h>
#include <bcrypt.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#pragma comment(lib, "Bcrypt.lib")

namespace
{
    constexpr ULONG HASH_ITERATIONS = 120000;
    constexpr ULONG SALT_SIZE = 16;
    constexpr ULONG HASH_SIZE = 32;

    const char* HEX = "0123456789abcdef";

    std::string ToHex(const unsigned char* data, size_t size)
    {
        std::string result;
        result.reserve(size * 2);

        for (size_t i = 0; i < size; ++i)
        {
            result += HEX[data[i] >> 4];
            result += HEX[data[i] & 0x0f];
        }

        return result;
    }

    int HexValue(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool FromHex(const std::string& hex, std::vector<unsigned char>& result)
    {
        if (hex.size() % 2 != 0)
            return false;

        result.clear();
        result.reserve(hex.size() / 2);

        for (size_t i = 0; i < hex.size(); i += 2)
        {
            int high = HexValue(hex[i]);
            int low = HexValue(hex[i + 1]);

            if (high < 0 || low < 0)
                return false;

            result.push_back(
                static_cast<unsigned char>((high << 4) | low));
        }

        return true;
    }

    bool DeriveHash(
        const std::string& password,
        const unsigned char* salt,
        ULONG saltSize,
        ULONG iterations,
        unsigned char* output,
        ULONG outputSize)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;

        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            BCRYPT_ALG_HANDLE_HMAC_FLAG);

        if (status < 0)
            return false;

        status = BCryptDeriveKeyPBKDF2(
            algorithm,
            reinterpret_cast<PUCHAR>(
                const_cast<char*>(password.data())),
            static_cast<ULONG>(password.size()),
            const_cast<PUCHAR>(salt),
            saltSize,
            iterations,
            output,
            outputSize,
            0);

        BCryptCloseAlgorithmProvider(algorithm, 0);
        return status >= 0;
    }

    bool HashPassword(const std::string& password, std::string& storedHash)
    {
        unsigned char salt[SALT_SIZE]{};
        unsigned char hash[HASH_SIZE]{};

        if (BCryptGenRandom(
            nullptr, salt, SALT_SIZE,
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            return false;

        if (!DeriveHash(
            password, salt, SALT_SIZE,
            HASH_ITERATIONS, hash, HASH_SIZE))
            return false;

        storedHash =
            "pbkdf2-sha256$" +
            std::to_string(HASH_ITERATIONS) + "$" +
            ToHex(salt, SALT_SIZE) + "$" +
            ToHex(hash, HASH_SIZE);

        SecureZeroMemory(hash, sizeof(hash));
        return true;
    }

    bool VerifyPassword(
        const std::string& password,
        const std::string& storedHash)
    {
        const std::string prefix = "pbkdf2-sha256$";

        if (storedHash.rfind(prefix, 0) != 0)
            return false;

        size_t first = storedHash.find('$', prefix.size());
        if (first == std::string::npos)
            return false;

        size_t second = storedHash.find('$', first + 1);
        if (second == std::string::npos)
            return false;

        ULONG iterations = 0;

        try
        {
            size_t used = 0;
            unsigned long parsed = std::stoul(
                storedHash.substr(prefix.size(),
                    first - prefix.size()), &used);

            if (used != first - prefix.size() ||
                parsed < 10000 || parsed > 1000000)
                return false;

            iterations = static_cast<ULONG>(parsed);
        }
        catch (...)
        {
            return false;
        }

        std::vector<unsigned char> salt;
        std::vector<unsigned char> expected;

        if (!FromHex(
            storedHash.substr(first + 1, second - first - 1), salt) ||
            !FromHex(storedHash.substr(second + 1), expected))
            return false;

        if (salt.size() != SALT_SIZE || expected.size() != HASH_SIZE)
            return false;

        unsigned char actual[HASH_SIZE]{};

        if (!DeriveHash(
            password, salt.data(),
            static_cast<ULONG>(salt.size()),
            iterations, actual, HASH_SIZE))
            return false;

        unsigned char difference = 0;

        for (size_t i = 0; i < HASH_SIZE; ++i)
            difference |= actual[i] ^ expected[i];

        SecureZeroMemory(actual, sizeof(actual));
        return difference == 0;
    }
}

DatabaseService& DatabaseService::Instance()
{
    static DatabaseService instance;
    return instance;
}

DatabaseService::~DatabaseService()
{
    Disconnect();
}

bool DatabaseService::Connect(
    const char* host,
    const char* username,
    const char* password,
    const char* database,
    unsigned int port)
{
    Disconnect();
    m_lastError.clear();

    char* environmentPassword = nullptr;
    size_t passwordLength = 0;

    if (password == nullptr)
    {
        errno_t result = _dupenv_s(
            &environmentPassword, &passwordLength,
            "CHATPROJ_DB_PASSWORD");

        if (result != 0 || environmentPassword == nullptr)
        {
            m_lastError =
                "CHATPROJ_DB_PASSWORD environment variable is missing";
            return false;
        }
    }

    const char* actualPassword =
        password != nullptr ? password : environmentPassword;

    m_connection = mysql_init(nullptr);

    if (m_connection == nullptr)
    {
        m_lastError = "mysql_init failed";

        if (environmentPassword)
            free(environmentPassword);

        return false;
    }

    if (mysql_real_connect(
        m_connection, host, username, actualPassword,
        database, port, nullptr, 0) == nullptr)
    {
        m_lastError = mysql_error(m_connection);
        mysql_close(m_connection);
        m_connection = nullptr;

        if (environmentPassword)
            free(environmentPassword);

        return false;
    }

    if (environmentPassword)
        free(environmentPassword);

    if (mysql_set_character_set(m_connection, "utf8mb4") != 0)
    {
        m_lastError = mysql_error(m_connection);
        Disconnect();
        return false;
    }

    m_lastError.clear();
    return true;
}

void DatabaseService::Disconnect()
{
    if (m_connection != nullptr)
    {
        mysql_close(m_connection);
        m_connection = nullptr;
    }
}

bool DatabaseService::IsConnected() const
{
    return m_connection != nullptr;
}

std::string DatabaseService::GetLastError() const
{
    return m_lastError;
}

bool DatabaseService::RegisterUser(
    const std::string& username,
    const std::string& email,
    const std::string& password,
    const std::string& displayName)
{
    if (!m_connection)
    {
        m_lastError = "Database is not connected";
        return false;
    }

    if (username.empty() || email.empty() ||
        displayName.empty() || password.size() < 8)
    {
        m_lastError = "Invalid registration fields";
        return false;
    }

    std::string passwordHash;

    if (!HashPassword(password, passwordHash))
    {
        m_lastError = "Windows BCrypt password hashing failed";
        return false;
    }

    const char* query =
        "INSERT INTO users "
        "(username, email, password_hash, display_name) "
        "VALUES (?, ?, ?, ?)";

    MYSQL_STMT* stmt = mysql_stmt_init(m_connection);

    if (!stmt)
    {
        m_lastError = mysql_error(m_connection);
        return false;
    }

    bool success = false;

    if (mysql_stmt_prepare(
        stmt, query, static_cast<unsigned long>(strlen(query))) != 0)
    {
        m_lastError = mysql_stmt_error(stmt);
    }
    else
    {
        const std::string* values[] =
        {
            &username, &email, &passwordHash, &displayName
        };

        MYSQL_BIND params[4]{};

        for (unsigned int i = 0; i < 4; ++i)
        {
            params[i].buffer_type = MYSQL_TYPE_STRING;
            params[i].buffer =
                const_cast<char*>(values[i]->c_str());
            params[i].buffer_length =
                static_cast<unsigned long>(values[i]->size());
        }

        if (mysql_stmt_bind_param(stmt, params) != 0)
            m_lastError = mysql_stmt_error(stmt);
        else if (mysql_stmt_execute(stmt) != 0)
            m_lastError = mysql_stmt_error(stmt);
        else
        {
            success = true;
            m_lastError.clear();
        }
    }

    mysql_stmt_close(stmt);
    return success;
}

bool DatabaseService::AuthenticateUser(
    const std::string& usernameOrEmail,
    const std::string& password,
    long long& userId,
    std::string& displayName)
{
    if (!m_connection)
    {
        m_lastError = "Database is not connected";
        return false;
    }

    const char* query =
        "SELECT id, display_name, password_hash FROM users "
        "WHERE username = ? OR email = ? LIMIT 1";

    MYSQL_STMT* stmt = mysql_stmt_init(m_connection);

    if (!stmt)
    {
        m_lastError = mysql_error(m_connection);
        return false;
    }

    bool success = false;

    if (mysql_stmt_prepare(
        stmt, query, static_cast<unsigned long>(strlen(query))) != 0)
    {
        m_lastError = mysql_stmt_error(stmt);
    }
    else
    {
        MYSQL_BIND params[2]{};

        for (int i = 0; i < 2; ++i)
        {
            params[i].buffer_type = MYSQL_TYPE_STRING;
            params[i].buffer =
                const_cast<char*>(usernameOrEmail.c_str());
            params[i].buffer_length =
                static_cast<unsigned long>(usernameOrEmail.size());
        }

        if (mysql_stmt_bind_param(stmt, params) != 0 ||
            mysql_stmt_execute(stmt) != 0)
        {
            m_lastError = mysql_stmt_error(stmt);
        }
        else
        {
            long long foundId = 0;
            char nameBuffer[101]{};
            char hashBuffer[256]{};
            unsigned long nameLength = 0;
            unsigned long hashLength = 0;
            bool nameNull = false;
            bool hashNull = false;

            MYSQL_BIND results[3]{};

            results[0].buffer_type = MYSQL_TYPE_LONGLONG;
            results[0].buffer = &foundId;
            results[0].is_unsigned = 0;

            results[1].buffer_type = MYSQL_TYPE_STRING;
            results[1].buffer = nameBuffer;
            results[1].buffer_length = sizeof(nameBuffer) - 1;
            results[1].length = &nameLength;
            results[1].is_null = &nameNull;

            results[2].buffer_type = MYSQL_TYPE_STRING;
            results[2].buffer = hashBuffer;
            results[2].buffer_length = sizeof(hashBuffer) - 1;
            results[2].length = &hashLength;
            results[2].is_null = &hashNull;

            if (mysql_stmt_bind_result(stmt, results) != 0)
            {
                m_lastError = mysql_stmt_error(stmt);
            }
            else
            {
                int fetched = mysql_stmt_fetch(stmt);

                if (fetched == 0 || fetched == MYSQL_DATA_TRUNCATED)
                {
                    const size_t safeNameLength =
                        nameLength < sizeof(nameBuffer)
                        ? nameLength : sizeof(nameBuffer) - 1;

                    const size_t safeHashLength =
                        hashLength < sizeof(hashBuffer)
                        ? hashLength : sizeof(hashBuffer) - 1;

                    std::string storedHash(hashBuffer, safeHashLength);

                    if (!nameNull && !hashNull &&
                        VerifyPassword(password, storedHash))
                    {
                        userId = foundId;
                        displayName.assign(nameBuffer, safeNameLength);
                        success = true;
                        m_lastError.clear();
                    }
                    else
                    {
                        m_lastError =
                            "Invalid username/email or password";
                    }
                }
                else if (fetched == MYSQL_NO_DATA)
                {
                    m_lastError =
                        "Invalid username/email or password";
                }
                else
                {
                    m_lastError = mysql_stmt_error(stmt);
                }
            }
        }
    }

    mysql_stmt_close(stmt);
    return success;
}

// Used by the chat server. Reuses the existing secure authentication.
bool DatabaseService::AuthenticateForChat(
    const std::string& usernameOrEmail,
    const std::string& password,
    long long& userId,
    std::string& displayName)
{
    return AuthenticateUser(
        usernameOrEmail, password, userId, displayName);
}

// Save PRIVATE, GROUP, or BROADCAST messages in the existing messages table.
// Requires messages.recipient_id and messages.message_type columns.
bool DatabaseService::SaveMessage(
    long long conversationId,
    long long senderId,
    long long recipientId,
    const std::string& messageType,
    const std::string& messageText)
{
    if (m_connection == nullptr)
    {
        m_lastError = "Database is not connected";
        return false;
    }

    if (conversationId <= 0 || senderId <= 0 ||
        (messageType != "PRIVATE" && messageType != "GROUP" && messageType != "BROADCAST"))
    {
        m_lastError = "Invalid message details";
        return false;
    }

    if (messageType == "PRIVATE" && recipientId <= 0)
    {
        m_lastError = "Private messages require a recipient ID";
        return false;
    }

    const char* query =
        "INSERT INTO messages "
        "(conversation_id, sender_id, recipient_id, message_type, message_text) "
        "VALUES (?, ?, ?, ?, ?)";

    MYSQL_STMT* stmt = mysql_stmt_init(m_connection);
    if (stmt == nullptr)
    {
        m_lastError = mysql_error(m_connection);
        return false;
    }

    bool success = false;

    if (mysql_stmt_prepare(
        stmt, query, static_cast<unsigned long>(strlen(query))) != 0)
    {
        m_lastError = mysql_stmt_error(stmt);
    }
    else
    {
        MYSQL_BIND params[5]{};
        bool recipientIsNull = (messageType == "GROUP" || messageType == "BROADCAST");

        params[0].buffer_type = MYSQL_TYPE_LONGLONG;
        params[0].buffer = &conversationId;

        params[1].buffer_type = MYSQL_TYPE_LONGLONG;
        params[1].buffer = &senderId;

        params[2].buffer_type = MYSQL_TYPE_LONGLONG;
        params[2].buffer = &recipientId;
        params[2].is_null = &recipientIsNull;

        params[3].buffer_type = MYSQL_TYPE_STRING;
        params[3].buffer = const_cast<char*>(messageType.c_str());
        params[3].buffer_length =
            static_cast<unsigned long>(messageType.size());

        params[4].buffer_type = MYSQL_TYPE_STRING;
        params[4].buffer = const_cast<char*>(messageText.c_str());
        params[4].buffer_length =
            static_cast<unsigned long>(messageText.size());

        if (mysql_stmt_bind_param(stmt, params) != 0)
        {
            m_lastError = mysql_stmt_error(stmt);
        }
        else if (mysql_stmt_execute(stmt) != 0)
        {
            m_lastError = mysql_stmt_error(stmt);
        }
        else
        {
            success = true;
            m_lastError.clear();
        }
    }

    mysql_stmt_close(stmt);
    return success;
}

// Persist the user's current online/offline status in users.is_online.
bool DatabaseService::SetUserOnline(long long userId, bool online)
{
    if (m_connection == nullptr)
    {
        m_lastError = "Database is not connected";
        return false;
    }

    if (userId <= 0)
    {
        m_lastError = "Invalid user ID";
        return false;
    }

    const char* query = "UPDATE users SET is_online = ? WHERE id = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(m_connection);
    if (stmt == nullptr)
    {
        m_lastError = mysql_error(m_connection);
        return false;
    }

    signed char onlineValue = online ? 1 : 0;
    MYSQL_BIND params[2]{};

    params[0].buffer_type = MYSQL_TYPE_TINY;
    params[0].buffer = &onlineValue;

    params[1].buffer_type = MYSQL_TYPE_LONGLONG;
    params[1].buffer = &userId;

    bool success = false;
    if (mysql_stmt_prepare(
        stmt, query, static_cast<unsigned long>(strlen(query))) != 0)
    {
        m_lastError = mysql_stmt_error(stmt);
    }
    else if (mysql_stmt_bind_param(stmt, params) != 0)
    {
        m_lastError = mysql_stmt_error(stmt);
    }
    else if (mysql_stmt_execute(stmt) != 0)
    {
        m_lastError = mysql_stmt_error(stmt);
    }
    else
    {
        success = true;
        m_lastError.clear();
    }

    mysql_stmt_close(stmt);
    return success;
}

