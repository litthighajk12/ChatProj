
#define WIN32_LEAN_AND_MEAN

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include "MessageFraming.h"

#pragma comment(lib, "Ws2_32.lib")

// Send the entire message, even if send() transmits only part of it.
bool SendAll(SOCKET socket, const std::string& data)
{
    size_t offset = 0;

    while (offset < data.size())
    {
        int sent = send(
            socket,
            data.data() + offset,
            static_cast<int>(data.size() - offset),
            0
        );

        if (sent == SOCKET_ERROR || sent == 0)
            return false;

        offset += static_cast<size_t>(sent);
    }

    return true;
}

int main()
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cout << "Winsock initialization failed.\n";
        return 1;
    }

    // Create TCP socket
    SOCKET clientSocket = socket(
        AF_INET, SOCK_STREAM, IPPROTO_TCP
    );

    if (clientSocket == INVALID_SOCKET)
    {
        std::cout << "TCP socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(5000);

    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (connect(
        clientSocket,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) == SOCKET_ERROR)
    {
        std::cout << "Connection failed: "
            << WSAGetLastError() << '\n';

        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    std::atomic<bool> running{ true };

    // TCP receiver thread
    std::thread receiver([&]()
        {
            char buffer[1024];
            std::string receiveBuffer;

            while (running)
            {
                int received = recv(
                    clientSocket,
                    buffer,
                    sizeof(buffer),
                    0
                );

                if (received <= 0)
                {
                    running = false;
                    break;
                }

                // Append bytes; a receive may contain partial
                // messages or several complete messages.
                receiveBuffer.append(buffer, received);

                std::vector<std::string> messages;

                if (!ExtractMessages(receiveBuffer, messages))
                {
                    std::cout << "\nInvalid message framing received.\n";
                    running = false;
                    shutdown(clientSocket, SD_BOTH);
                    break;
                }

                for (const auto& message : messages)
                {
                    if (message.rfind("WELCOME|", 0) == 0)
                    {
                        std::string idText = message.substr(8);

                        std::cout << "\nConnected! Your client ID is "
                            << idText << ".\n";
                    }
                    else
                    {
                        std::cout << "\n" << message << '\n';
                    }

                    std::cout << "You: ";
                    std::cout.flush();
                }
            }

            running = false;
        });

    std::cout
        << "Connected! Type messages. Enter /exit to quit.\n";

    // Create UDP socket for heartbeats
    SOCKET udpSocket = socket(
        AF_INET, SOCK_DGRAM, IPPROTO_UDP
    );

    if (udpSocket == INVALID_SOCKET)
    {
        std::cout << "UDP socket creation failed: "
            << WSAGetLastError() << '\n';

        running = false;
        shutdown(clientSocket, SD_BOTH);

        if (receiver.joinable())
            receiver.join();

        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    // UDP heartbeat destination
    sockaddr_in udpServerAddress{};
    udpServerAddress.sin_family = AF_INET;
    udpServerAddress.sin_port = htons(5001);

    inet_pton(
        AF_INET,
        "127.0.0.1",
        &udpServerAddress.sin_addr
    );

    std::atomic<bool> heartbeatRunning{ true };

    std::thread heartbeatThread([&]()
        {
            const char heartbeat[] = "HEARTBEAT";

            while (heartbeatRunning)
            {
                int result = sendto(
                    udpSocket,
                    heartbeat,
                    sizeof(heartbeat) - 1,
                    0,
                    reinterpret_cast<sockaddr*>(
                        &udpServerAddress),
                    sizeof(udpServerAddress)
                );

                if (result == SOCKET_ERROR)
                {
                    if (heartbeatRunning)
                    {
                        std::cout
                            << "\nUDP heartbeat send failed: "
                            << WSAGetLastError() << '\n';
                    }

                    break;
                }

                // Avoid printing every heartbeat so chat output
                // remains readable.

                std::this_thread::sleep_for(
                    std::chrono::seconds(5)
                );
            }
        });

    // Main message-sending loop
    std::string message;

    while (running)
    {
        std::cout << "\nChoose mode: 1 = Broadcast, 2 = Private, /exit = Quit\n";
        std::cout << "Mode: ";

        std::string mode;
        if (!std::getline(std::cin, mode))
            break;

        if (mode == "/exit")
            break;

        if (mode != "1" && mode != "2")
        {
            std::cout << "Invalid mode.\n";
            continue;
        }

        int recipientId = 0;

        if (mode == "2")
        {
            std::cout << "Recipient client ID: ";

            std::string recipientInput;
            if (!std::getline(std::cin, recipientInput))
                break;

            try
            {
                size_t parsedLength = 0;
                recipientId = std::stoi(
                    recipientInput, &parsedLength
                );

                if (parsedLength != recipientInput.size() ||
                    recipientId <= 0)
                {
                    std::cout << "Invalid client ID.\n";
                    continue;
                }
            }
            catch (...)
            {
                std::cout << "Invalid client ID.\n";
                continue;
            }
        }

        std::cout << "Message: ";

        if (!std::getline(std::cin, message))
            break;

        if (message == "/exit")
            break;

        if (message.empty())
            continue;

        std::string protocolMessage;

        if (mode == "1")
        {
            protocolMessage = "B|" + message;
        }
        else
        {
            protocolMessage =
                "P|" + std::to_string(recipientId)
                + "|" + message;
        }

        std::string frame = EncodeMessage(protocolMessage) + '\n';

        if (!SendAll(clientSocket, frame))
        {
            std::cout << "TCP send failed.\n";
            break;
        }
    }

    // Stop networking threads
    running = false;
    heartbeatRunning = false;

    shutdown(clientSocket, SD_BOTH);

    // Closing UDP unblocks any pending UDP operation if needed.
    closesocket(udpSocket);

    if (receiver.joinable())
        receiver.join();

    if (heartbeatThread.joinable())
        heartbeatThread.join();

    closesocket(clientSocket);
    WSACleanup();

    std::cout << "Client disconnected.\n";
    return 0;
}
