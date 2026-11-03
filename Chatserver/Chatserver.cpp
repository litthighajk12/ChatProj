#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mysql.h>
#include <rdkafkacpp.h>
#include "MessageFraming.h"
#include "DatabaseService.h"

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <chrono>
#include <cstdlib>

#pragma comment(lib, "Ws2_32.lib")

struct Client
{
    SOCKET socket = INVALID_SOCKET;
    int clientId = 0;
    long long userId = 0;
    std::string displayName;
    std::mutex sendMutex;
    std::string receiveBuffer;
    std::atomic<bool> connected{ true };
    std::atomic<bool> authenticated{ false };
};

struct IoOperation
{
    OVERLAPPED overlapped{};
    WSABUF buffer{};
    char data[2048]{};
    DWORD flags = 0;
    std::shared_ptr<Client> client;
};

static HANDLE g_iocp = nullptr;
static RdKafka::Producer* g_kafkaProducer = nullptr;
static RdKafka::Conf* g_kafkaConfig = nullptr;
static std::mutex g_kafkaMutex;
static std::mutex g_clientsMutex;
static std::vector<std::shared_ptr<Client>> g_clients;
static std::atomic<int> g_nextClientId{ 1 };
static SOCKET g_udpSocket = INVALID_SOCKET;
static MYSQL* g_sql = nullptr;
static std::mutex g_sqlMutex;
static std::mutex g_databaseServiceMutex;
static std::atomic<bool> g_stopping{ false };

using Clock = std::chrono::steady_clock;
struct PresenceInfo { Clock::time_point lastHeartbeat; bool active = true; };
static std::mutex g_presenceMutex;
static std::unordered_map<std::string, PresenceInfo> g_lastHeartbeat;

static std::string Frame(const std::string& message)
{
    return EncodeMessage(message) + '\n';
}

static bool SendAll(SOCKET socket, const std::string& data)
{
    size_t offset = 0;
    while (offset < data.size())
    {
        int sent = send(socket, data.data() + offset,
            static_cast<int>(data.size() - offset), 0);
        if (sent == SOCKET_ERROR || sent == 0) return false;
        offset += static_cast<size_t>(sent);
    }
    return true;
}

static bool SendToClient(const std::shared_ptr<Client>& client,
    const std::string& message)
{
    if (!client || !client->connected) return false;
    std::lock_guard<std::mutex> lock(client->sendMutex);
    if (!client->connected || client->socket == INVALID_SOCKET) return false;
    return SendAll(client->socket, Frame(message));
}

static void PublishToKafka(const std::string& message)
{
    std::lock_guard<std::mutex> lock(g_kafkaMutex);
    if (!g_kafkaProducer) return;
    std::string error;
    RdKafka::Topic* topic = RdKafka::Topic::create(
        g_kafkaProducer, "chat-events", nullptr, error);
    if (!topic)
    {
        std::cerr << "Kafka topic error: " << error << '\n';
        return;
    }
    auto result = g_kafkaProducer->produce(topic,
        RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
        const_cast<char*>(message.data()), message.size(), nullptr, 0, nullptr);
    delete topic;
    g_kafkaProducer->poll(0);
    if (result != RdKafka::ERR_NO_ERROR)
        std::cerr << "Kafka publish failed: " << RdKafka::err2str(result) << '\n';
}

static bool InitializeKafkaProducer()
{
    std::string error;
    g_kafkaConfig = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    if (!g_kafkaConfig) return false;
    if (g_kafkaConfig->set("bootstrap.servers", "localhost:9092", error)
        != RdKafka::Conf::CONF_OK)
    {
        std::cerr << "Kafka config error: " << error << '\n';
        delete g_kafkaConfig; g_kafkaConfig = nullptr; return false;
    }
    g_kafkaProducer = RdKafka::Producer::create(g_kafkaConfig, error);
    if (!g_kafkaProducer)
    {
        std::cerr << "Kafka producer error: " << error << '\n';
        delete g_kafkaConfig; g_kafkaConfig = nullptr; return false;
    }
    std::cout << "Kafka producer ready.\n";
    return true;
}

static bool InitializeDatabase()
{
    // The DatabaseService connection is used for password verification and writes.
    {
        std::lock_guard<std::mutex> lock(g_databaseServiceMutex);
        if (!DatabaseService::Instance().Connect(
            "127.0.0.1", "root", nullptr, "chatproj", 3306))
        {
            std::cerr << "DatabaseService connection failed: "
                << DatabaseService::Instance().GetLastError() << '\n';
            return false;
        }
    }

    // A second connection is used for conversation/membership lookups.
    const char* password = std::getenv("CHATPROJ_DB_PASSWORD");
    if (!password)
    {
        std::cerr << "CHATPROJ_DB_PASSWORD is not set.\n";
        return false;
    }
    g_sql = mysql_init(nullptr);
    if (!g_sql || !mysql_real_connect(g_sql, "127.0.0.1", "root",
        password, "chatproj", 3306, nullptr, 0))
    {
        std::cerr << "Conversation DB connection failed: "
            << (g_sql ? mysql_error(g_sql) : "mysql_init failed") << '\n';
        if (g_sql) { mysql_close(g_sql); g_sql = nullptr; }
        return false;
    }
    mysql_set_character_set(g_sql, "utf8mb4");
    std::cout << "MySQL connected to chatproj.\n";
    return true;
}

// The functions below ending in Unlocked must only be called while g_sqlMutex is held.
static std::string EscapeSqlUnlocked(const std::string& value)
{
    std::string out(value.size() * 2 + 1, '\0');
    unsigned long n = mysql_real_escape_string(g_sql, &out[0],
        value.c_str(), static_cast<unsigned long>(value.size()));
    out.resize(n);
    return out;
}

static bool QueryUnlocked(const std::string& sql)
{
    if (mysql_query(g_sql, sql.c_str()) != 0)
    {
        std::cerr << "MySQL query failed: " << mysql_error(g_sql) << '\n';
        return false;
    }
    return true;
}

static bool ScalarUnlocked(const std::string& sql, long long& value, bool& found)
{
    found = false;
    if (!QueryUnlocked(sql)) return false;
    MYSQL_RES* result = mysql_store_result(g_sql);
    if (!result) return mysql_field_count(g_sql) == 0;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row && row[0]) { value = std::strtoll(row[0], nullptr, 10); found = true; }
    mysql_free_result(result);
    return true;
}

static bool UserExists(long long userId)
{
    std::lock_guard<std::mutex> lock(g_sqlMutex);
    long long value = 0; bool found = false;
    return ScalarUnlocked("SELECT id FROM users WHERE id=" +
        std::to_string(userId) + " LIMIT 1", value, found) && found;
}

static bool IsGroupMember(long long conversationId, long long userId)
{
    std::lock_guard<std::mutex> lock(g_sqlMutex);
    long long value = 0; bool found = false;
    return ScalarUnlocked("SELECT user_id FROM conversation_members WHERE conversation_id=" +
        std::to_string(conversationId) + " AND user_id=" +
        std::to_string(userId) + " LIMIT 1", value, found) && found;
}

static bool IsGroupConversation(long long conversationId)
{
    std::lock_guard<std::mutex> lock(g_sqlMutex);
    long long value = 0; bool found = false;
    return ScalarUnlocked("SELECT id FROM conversations WHERE id=" +
        std::to_string(conversationId) + " AND is_group=1 LIMIT 1", value, found) && found;
}

static bool GetGroupMembers(long long conversationId, std::vector<long long>& userIds)
{
    std::lock_guard<std::mutex> lock(g_sqlMutex);
    userIds.clear();
    if (!QueryUnlocked("SELECT user_id FROM conversation_members WHERE conversation_id=" +
        std::to_string(conversationId))) return false;
    MYSQL_RES* result = mysql_store_result(g_sql);
    if (!result) return false;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr)
        if (row[0]) userIds.push_back(std::strtoll(row[0], nullptr, 10));
    mysql_free_result(result);
    return true;
}

static bool GetOrCreatePrivateConversation(long long userA, long long userB,
    long long& conversationId)
{
    if (userA <= 0 || userB <= 0 || userA == userB) return false;
    std::lock_guard<std::mutex> lock(g_sqlMutex);

    const std::string findSql =
        "SELECT c.id FROM conversations c "
        "JOIN conversation_members a ON a.conversation_id=c.id AND a.user_id=" +
        std::to_string(userA) +
        " JOIN conversation_members b ON b.conversation_id=c.id AND b.user_id=" +
        std::to_string(userB) +
        " WHERE c.is_group=0 LIMIT 1";
    long long foundId = 0; bool found = false;
    if (!ScalarUnlocked(findSql, foundId, found)) return false;
    if (found) { conversationId = foundId; return true; }

    if (!QueryUnlocked("INSERT INTO conversations (name, is_group, created_by) "
        "VALUES ('Private chat', 0, " + std::to_string(userA) + ")")) return false;
    const long long newId = static_cast<long long>(mysql_insert_id(g_sql));
    if (newId <= 0) return false;
    if (!QueryUnlocked("INSERT INTO conversation_members (conversation_id, user_id) VALUES (" +
        std::to_string(newId) + "," + std::to_string(userA) + "),(" +
        std::to_string(newId) + "," + std::to_string(userB) + ")"))
    {
        // Do not use the newly created conversation if membership creation failed.
        return false;
    }
    conversationId = newId;
    return true;
}


// One shared conversation row is used to persist general broadcasts.
// Broadcast delivery still goes to every authenticated client; membership is not required.
static bool GetOrCreateBroadcastConversation(long long creatorUserId,
    long long& conversationId)
{
    if (creatorUserId <= 0)
        return false;

    std::lock_guard<std::mutex> lock(g_sqlMutex);

    long long foundId = 0;
    bool found = false;
    if (!ScalarUnlocked(
        "SELECT id FROM conversations "
        "WHERE is_group=1 AND name='Broadcast' LIMIT 1",
        foundId, found))
        return false;

    if (found)
    {
        conversationId = foundId;
        return true;
    }

    if (!QueryUnlocked(
        "INSERT INTO conversations (name, is_group, created_by) VALUES "
        "('Broadcast', 1, " + std::to_string(creatorUserId) + ")"))
        return false;

    const long long newId = static_cast<long long>(mysql_insert_id(g_sql));
    if (newId <= 0)
        return false;

    conversationId = newId;
    return true;
}

static std::vector<std::shared_ptr<Client>> AuthenticatedClientsSnapshot()
{
    std::vector<std::shared_ptr<Client>> result;
    std::lock_guard<std::mutex> lock(g_clientsMutex);
    for (const auto& c : g_clients)
        if (c->connected && c->authenticated) result.push_back(c);
    return result;
}

static std::shared_ptr<Client> FindConnectedUser(long long userId)
{
    std::lock_guard<std::mutex> lock(g_clientsMutex);
    for (const auto& c : g_clients)
        if (c->connected && c->authenticated && c->userId == userId) return c;
    return nullptr;
}

static void SendAuthError(const std::shared_ptr<Client>& client, const std::string& reason)
{
    SendToClient(client, "AUTH_ERROR|" + reason);
}

static void ProcessAuthentication(const std::shared_ptr<Client>& client,
    const std::string& message)
{
    if (client->authenticated)
    {
        SendToClient(client, "ERROR|Already authenticated");
        return;
    }
    if (message.rfind("AUTH|", 0) != 0)
    {
        SendAuthError(client, "Send AUTH first");
        shutdown(client->socket, SD_BOTH);
        return;
    }

    const size_t separator = message.find('|', 5);
    if (separator == std::string::npos || separator == 5 || separator + 1 >= message.size())
    {
        SendAuthError(client, "Invalid credentials format");
        shutdown(client->socket, SD_BOTH);
        return;
    }

    const std::string username = message.substr(5, separator - 5);
    const std::string password = message.substr(separator + 1);
    long long userId = 0;
    std::string displayName;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(g_databaseServiceMutex);
        ok = DatabaseService::Instance().AuthenticateForChat(
            username, password, userId, displayName);
    }
    if (!ok || userId <= 0)
    {
        SendAuthError(client, "Invalid username/email or password");
        shutdown(client->socket, SD_BOTH);
        return;
    }

    client->userId = userId;
    client->displayName = displayName;
    client->authenticated = true;
    {
        std::lock_guard<std::mutex> lock(g_databaseServiceMutex);
        if (!DatabaseService::Instance().SetUserOnline(userId, true))
            std::cerr << "Could not set user online: "
            << DatabaseService::Instance().GetLastError() << '\n';
    }

    SendToClient(client, "WELCOME|" + std::to_string(client->clientId) + "|" +
        std::to_string(client->userId) + "|" + client->displayName);
    PublishToKafka("LOGIN|user=" + std::to_string(userId));
    std::cout << "Authenticated client " << client->clientId
        << " as user " << userId << " (" << displayName << ")\n";
}

static void ProcessMessage(const std::shared_ptr<Client>& sender,
    const std::string& message)
{
    if (!sender->authenticated)
    {
        ProcessAuthentication(sender, message);
        return;
    }

    // Private: P|recipientUserId|message text
    if (message.rfind("P|", 0) == 0)
    {
        size_t sep = message.find('|', 2);
        if (sep == std::string::npos) { SendToClient(sender, "ERROR|Invalid private format"); return; }
        long long recipientId = 0;
        try
        {
            size_t used = 0;
            recipientId = std::stoll(message.substr(2, sep - 2), &used);
            if (used != sep - 2 || recipientId <= 0 || recipientId == sender->userId)
                throw std::invalid_argument("recipient");
        }
        catch (...) { SendToClient(sender, "ERROR|Invalid recipient user ID"); return; }

        const std::string text = message.substr(sep + 1);
        if (!UserExists(recipientId)) { SendToClient(sender, "ERROR|Recipient does not exist"); return; }
        long long conversationId = 0;
        if (!GetOrCreatePrivateConversation(sender->userId, recipientId, conversationId))
        {
            SendToClient(sender, "ERROR|Could not create private conversation; check conversation schema"); return;
        }

        bool saved = false;
        {
            std::lock_guard<std::mutex> lock(g_databaseServiceMutex);
            saved = DatabaseService::Instance().SaveMessage(
                conversationId, sender->userId, recipientId, "PRIVATE", text);
            if (!saved) std::cerr << "Save private message failed: "
                << DatabaseService::Instance().GetLastError() << '\n';
        }
        if (!saved) { SendToClient(sender, "ERROR|Message could not be saved"); return; }

        const std::string delivered = "P|" + std::to_string(sender->userId) + "|" + text;
        auto recipient = FindConnectedUser(recipientId);
        if (recipient) SendToClient(recipient, delivered);
        SendToClient(sender, "P|" + std::to_string(sender->userId) + "|" + text);
        PublishToKafka("PRIVATE|from=" + std::to_string(sender->userId) +
            "|to=" + std::to_string(recipientId) + "|" + text);
        return;
    }

    // Group: G|conversationId|message text. Membership is verified server-side.
    if (message.rfind("G|", 0) == 0)
    {
        size_t sep = message.find('|', 2);
        if (sep == std::string::npos) { SendToClient(sender, "ERROR|Invalid group format"); return; }
        long long conversationId = 0;
        try
        {
            size_t used = 0;
            conversationId = std::stoll(message.substr(2, sep - 2), &used);
            if (used != sep - 2 || conversationId <= 0) throw std::invalid_argument("group");
        }
        catch (...) { SendToClient(sender, "ERROR|Invalid conversation ID"); return; }

        if (!IsGroupConversation(conversationId) || !IsGroupMember(conversationId, sender->userId))
        {
            SendToClient(sender, "ERROR|You are not a member of this group"); return;
        }
        const std::string text = message.substr(sep + 1);
        bool saved = false;
        {
            std::lock_guard<std::mutex> lock(g_databaseServiceMutex);
            saved = DatabaseService::Instance().SaveMessage(
                conversationId, sender->userId, 0, "GROUP", text);
            if (!saved) std::cerr << "Save group message failed: "
                << DatabaseService::Instance().GetLastError() << '\n';
        }
        if (!saved) { SendToClient(sender, "ERROR|Message could not be saved"); return; }

        std::vector<long long> members;
        if (!GetGroupMembers(conversationId, members))
        {
            SendToClient(sender, "ERROR|Could not load group members"); return;
        }
        const std::string delivered = "G|" + std::to_string(conversationId) + "|" +
            std::to_string(sender->userId) + "|" + text;
        for (long long memberId : members)
        {
            auto recipient = FindConnectedUser(memberId);
            if (recipient) SendToClient(recipient, delivered);
        }
        PublishToKafka("GROUP|conversation=" + std::to_string(conversationId) +
            "|from=" + std::to_string(sender->userId) + "|" + text);
        return;
    }

    // Broadcast: B|message text. Persist once, then deliver to all authenticated clients.
    if (message.rfind("B|", 0) == 0)
    {
        const std::string text = message.substr(2);
        if (text.empty())
        {
            SendToClient(sender, "ERROR|Broadcast message cannot be empty");
            return;
        }

        long long conversationId = 0;
        if (!GetOrCreateBroadcastConversation(sender->userId, conversationId))
        {
            SendToClient(sender, "ERROR|Could not create broadcast conversation");
            std::cerr << "Create broadcast conversation failed for user "
                << sender->userId << '\n';
            return;
        }

        bool saved = false;
        {
            std::lock_guard<std::mutex> lock(g_databaseServiceMutex);
            saved = DatabaseService::Instance().SaveMessage(
                conversationId, sender->userId, 0, "BROADCAST", text);
            if (!saved)
                std::cerr << "Save broadcast message failed: "
                << DatabaseService::Instance().GetLastError() << '\n';
        }

        if (!saved)
        {
            SendToClient(sender, "ERROR|Broadcast could not be saved");
            return;
        }

        const std::string outgoing =
            "B|" + std::to_string(sender->userId) + "|" + text;
        for (const auto& recipient : AuthenticatedClientsSnapshot())
            SendToClient(recipient, outgoing);

        PublishToKafka(
            "BROADCAST|conversation=" + std::to_string(conversationId) +
            "|user=" + std::to_string(sender->userId) + "|" + text);
        std::cout << "Saved broadcast from user " << sender->userId
            << " in conversation " << conversationId << '\n';
        return;
    }

    SendToClient(sender, "ERROR|Unknown message type. Use P or G");
}

static bool PostReceive(const std::shared_ptr<Client>& client, IoOperation* op)
{
    op->client = client;
    op->buffer.buf = op->data;
    op->buffer.len = sizeof(op->data);
    op->flags = 0;
    DWORD received = 0;
    int result = WSARecv(client->socket, &op->buffer, 1, &received,
        &op->flags, &op->overlapped, nullptr);
    return result == 0 || WSAGetLastError() == WSA_IO_PENDING;
}

static void RemoveClient(const std::shared_ptr<Client>& client)
{
    bool expected = true;
    if (!client->connected.compare_exchange_strong(expected, false)) return;

    const long long userId = client->userId;
    {
        std::lock_guard<std::mutex> lock(client->sendMutex);
        if (client->socket != INVALID_SOCKET)
        {
            shutdown(client->socket, SD_BOTH);
            closesocket(client->socket);
            client->socket = INVALID_SOCKET;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        g_clients.erase(std::remove(g_clients.begin(), g_clients.end(), client), g_clients.end());
    }

    if (userId > 0)
    {
        bool anotherSession = false;
        {
            std::lock_guard<std::mutex> lock(g_clientsMutex);
            for (const auto& other : g_clients)
                if (other->connected && other->authenticated && other->userId == userId)
                {
                    anotherSession = true; break;
                }
        }
        if (!anotherSession)
        {
            std::lock_guard<std::mutex> lock(g_databaseServiceMutex);
            if (!DatabaseService::Instance().SetUserOnline(userId, false))
                std::cerr << "Could not set user offline: "
                << DatabaseService::Instance().GetLastError() << '\n';
        }
        PublishToKafka("LOGOUT|user=" + std::to_string(userId));
    }
    std::cout << "Client " << client->clientId << " disconnected.\n";
}

static void WorkerThread()
{
    while (!g_stopping)
    {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;
        BOOL ok = GetQueuedCompletionStatus(g_iocp, &bytes, &key, &overlapped, INFINITE);
        if (!overlapped) { if (g_stopping) return; continue; }
        auto* op = reinterpret_cast<IoOperation*>(overlapped);
        auto client = op->client;
        if (!ok || bytes == 0)
        {
            RemoveClient(client); delete op; continue;
        }
        if (!client->connected) { delete op; continue; }
        client->receiveBuffer.append(op->data, bytes);
        std::vector<std::string> messages;
        if (!ExtractMessages(client->receiveBuffer, messages))
        {
            std::cerr << "Invalid or oversized TCP frame.\n";
            RemoveClient(client); delete op; continue;
        }
        for (const auto& message : messages)
        {
            if (!client->connected) break;
            ProcessMessage(client, message);
        }
        ZeroMemory(&op->overlapped, sizeof(op->overlapped));
        if (!client->connected || !PostReceive(client, op))
        {
            RemoveClient(client); delete op;
        }
    }
}

static void UdpHeartbeatReceiver()
{
    char buffer[256];
    while (!g_stopping)
    {
        sockaddr_in sender{};
        int senderLength = sizeof(sender);
        int received = recvfrom(g_udpSocket, buffer, sizeof(buffer), 0,
            reinterpret_cast<sockaddr*>(&sender), &senderLength);
        if (received == SOCKET_ERROR)
        {
            if (g_stopping) return;
            std::cerr << "UDP receive failed: " << WSAGetLastError() << '\n';
            return;
        }
        char ip[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &sender.sin_addr, ip, sizeof(ip));
        const std::string key = std::string(ip) + ":" + std::to_string(ntohs(sender.sin_port));
        std::lock_guard<std::mutex> lock(g_presenceMutex);
        auto& presence = g_lastHeartbeat[key];
        presence.lastHeartbeat = Clock::now();
        presence.active = true;
    }
}

static void HeartbeatTimeoutChecker()
{
    while (!g_stopping)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto now = Clock::now();
        std::lock_guard<std::mutex> lock(g_presenceMutex);
        for (auto& item : g_lastHeartbeat)
        {
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                now - item.second.lastHeartbeat).count();
            if (item.second.active && seconds > 15)
            {
                item.second.active = false;
                std::cout << "[UDP] Heartbeat inactive: " << item.first << '\n';
            }
        }
    }
}

int main()
{
    if (!InitializeDatabase()) return 1;
    if (!InitializeKafkaProducer())
    {
        std::cerr << "Kafka initialization failed; server startup aborted.\n";
        return 1;
    }
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        std::cerr << "Winsock initialization failed.\n";
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        std::cerr << "TCP socket creation failed.\n"; WSACleanup(); return 1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(5000);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "TCP bind/listen failed: " << WSAGetLastError() << '\n';
        closesocket(listenSocket); WSACleanup(); return 1;
    }

    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!g_iocp)
    {
        std::cerr << "IOCP creation failed.\n"; closesocket(listenSocket); WSACleanup(); return 1;
    }
    unsigned int workerCount = std::thread::hardware_concurrency();
    if (workerCount == 0) workerCount = 2;
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < workerCount; ++i) workers.emplace_back(WorkerThread);

    g_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udpSocket == INVALID_SOCKET)
    {
        std::cerr << "UDP socket creation failed: " << WSAGetLastError() << '\n';
        return 1;
    }
    sockaddr_in udpAddress{};
    udpAddress.sin_family = AF_INET;
    udpAddress.sin_port = htons(5001);
    udpAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_udpSocket, reinterpret_cast<sockaddr*>(&udpAddress), sizeof(udpAddress)) == SOCKET_ERROR)
    {
        std::cerr << "UDP bind failed: " << WSAGetLastError() << '\n';
        return 1;
    }
    std::thread(UdpHeartbeatReceiver).detach();
    std::thread(HeartbeatTimeoutChecker).detach();

    std::cout << "Authenticated IOCP chat server listening on TCP 5000, UDP 5001.\n";
    while (true)
    {
        SOCKET clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
        {
            if (g_stopping) break;
            continue;
        }
        auto client = std::make_shared<Client>();
        client->socket = clientSocket;
        client->clientId = g_nextClientId++;
        if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, 0, 0))
        {
            closesocket(clientSocket); continue;
        }
        {
            std::lock_guard<std::mutex> lock(g_clientsMutex);
            g_clients.push_back(client);
        }
        std::cout << "TCP client connected: " << client->clientId << " (awaiting AUTH).\n";
        auto* op = new IoOperation{};
        if (!PostReceive(client, op)) { RemoveClient(client); delete op; }
    }
    return 0;
}
