#pragma once

#include <string>
#include <vector>
#include <utility>
#include <string>

// Escape special characters inside a message.
std::string EncodeMessage(const std::string& message)
{
    std::string result;

    for (char ch : message)
    {
        if (ch == '\\')
            result += "\\\\";
        else if (ch == '\n')
            result += "\\n";
        else if (ch == '\r')
            result += "\\r";
        else
            result += ch;
    }

    return result;
}

// Decode an escaped message.
bool DecodeMessage(const std::string& encoded, std::string& result)
{
    result.clear();

    for (size_t i = 0; i < encoded.size(); ++i)
    {
        if (encoded[i] != '\\')
        {
            result += encoded[i];
            continue;
        }

        if (++i >= encoded.size())
            return false;

        switch (encoded[i])
        {
        case '\\': result += '\\'; break;
        case 'n':  result += '\n'; break;
        case 'r':  result += '\r'; break;
        default: return false;
        }
    }

    return true;
}

// Extract all complete frames, leaving incomplete bytes buffered.
bool ExtractMessages(
    std::string& receiveBuffer,
    std::vector<std::string>& messages)
{
    constexpr size_t MAX_FRAME_SIZE = 64 * 1024;

    while (true)
    {
        size_t delimiter = receiveBuffer.find('\n');

        if (delimiter == std::string::npos)
        {
            if (receiveBuffer.size() > MAX_FRAME_SIZE)
                return false;

            return true;
        }

        if (delimiter > MAX_FRAME_SIZE)
            return false;

        std::string decoded;

        if (!DecodeMessage(
            receiveBuffer.substr(0, delimiter), decoded))
        {
            return false;
        }

        messages.push_back(std::move(decoded));
        receiveBuffer.erase(0, delimiter + 1);
    }
}