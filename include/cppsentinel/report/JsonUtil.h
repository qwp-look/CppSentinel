#pragma once

// Minimal JSON string escaping shared by the report serializers.
// The project deliberately avoids a JSON library: report.json / report.sarif
// are written by hand from plain structs.

#include <cstdio>
#include <string>
#include <string_view>

namespace cppsentinel::json
{

inline std::string escape(std::string_view input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input)
    {
        switch (c)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buf[8];
                const int written = std::snprintf(
                    buf, sizeof(buf), "\\u%04x",
                    static_cast<unsigned>(static_cast<unsigned char>(c)));
                if (written > 0)
                {
                    out.append(buf, static_cast<std::size_t>(written));
                }
            }
            else
            {
                out.push_back(c);
            }
            break;
        }
    }
    return out;
}

/// Percent-encode a '/'-separated relative path for use as a URI.
inline std::string uriEncodePath(std::string_view path)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(path.size() + 8);
    for (char c : path)
    {
        const auto byte = static_cast<unsigned char>(c);
        const bool unreserved =
            (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' ||
            byte == '_' || byte == '~' || byte == '/' || byte == ':';
        if (unreserved)
        {
            out.push_back(static_cast<char>(byte));
            continue;
        }
        out.push_back('%');
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

} // namespace cppsentinel::json
