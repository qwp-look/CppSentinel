// Recursive-descent JSON reader (RFC 8259 subset: no comments, no trailing commas).

#include "cppsentinel/util/Json.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace cppsentinel::json
{
namespace
{

class Parser
{
public:
    Parser(std::string_view text, std::string& error)
        : text_(text), error_(error)
    {
    }

    std::optional<Value> run()
    {
        skipWhitespace();
        auto value = parseValue();
        if (!value)
        {
            return std::nullopt;
        }
        skipWhitespace();
        if (pos_ != text_.size())
        {
            fail("unexpected trailing content");
            return std::nullopt;
        }
        return value;
    }

private:
    std::string_view text_;
    std::string& error_;
    std::size_t pos_ = 0;

    bool atEnd() const noexcept
    {
        return pos_ >= text_.size();
    }

    char peek() const noexcept
    {
        return atEnd() ? '\0' : text_[pos_];
    }

    bool consume(char expected) noexcept
    {
        if (peek() != expected)
        {
            return false;
        }
        ++pos_;
        return true;
    }

    void skipWhitespace() noexcept
    {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' ||
                            peek() == '\r'))
        {
            ++pos_;
        }
    }

    bool fail(std::string_view message)
    {
        if (error_.empty())
        {
            error_ = std::string(message) + " at offset " + std::to_string(pos_);
        }
        return false;
    }

    bool literal(std::string_view text)
    {
        if (text_.compare(pos_, text.size(), text) != 0)
        {
            return false;
        }
        pos_ += text.size();
        return true;
    }

    std::optional<Value> parseValue()
    {
        skipWhitespace();
        if (atEnd())
        {
            fail("unexpected end of input");
            return std::nullopt;
        }

        switch (peek())
        {
        case '{':
            return parseObject();
        case '[':
            return parseArray();
        case '"':
        {
            Value value;
            value.type = Value::Type::String;
            if (!parseString(value.string))
            {
                return std::nullopt;
            }
            return value;
        }
        case 't':
            if (literal("true"))
            {
                Value value;
                value.type = Value::Type::Bool;
                value.boolean = true;
                return value;
            }
            break;
        case 'f':
            if (literal("false"))
            {
                Value value;
                value.type = Value::Type::Bool;
                return value;
            }
            break;
        case 'n':
            if (literal("null"))
            {
                return Value{};
            }
            break;
        default:
            if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())))
            {
                return parseNumber();
            }
            break;
        }

        fail("unexpected token");
        return std::nullopt;
    }

    std::optional<Value> parseNumber()
    {
        const std::size_t start = pos_;
        if (peek() == '-')
        {
            ++pos_;
        }
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())))
        {
            ++pos_;
        }
        if (peek() == '.')
        {
            ++pos_;
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())))
            {
                ++pos_;
            }
        }
        if (peek() == 'e' || peek() == 'E')
        {
            ++pos_;
            if (peek() == '+' || peek() == '-')
            {
                ++pos_;
            }
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek())))
            {
                ++pos_;
            }
        }
        if (pos_ == start)
        {
            fail("invalid number");
            return std::nullopt;
        }

        Value value;
        value.type = Value::Type::Number;
        value.number = std::strtod(std::string(text_.substr(start, pos_ - start)).c_str(),
                                   nullptr);
        return value;
    }

    static void appendUtf8(std::string& out, std::uint32_t codePoint)
    {
        if (codePoint <= 0x7F)
        {
            out.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool parseHex4(std::uint32_t& out)
    {
        if (pos_ + 4 > text_.size())
        {
            return fail("truncated \\u escape");
        }
        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9')
            {
                out |= static_cast<std::uint32_t>(c - '0');
            }
            else if (c >= 'a' && c <= 'f')
            {
                out |= static_cast<std::uint32_t>(c - 'a' + 10);
            }
            else if (c >= 'A' && c <= 'F')
            {
                out |= static_cast<std::uint32_t>(c - 'A' + 10);
            }
            else
            {
                return fail("invalid \\u escape");
            }
        }
        return true;
    }

    bool parseString(std::string& out)
    {
        if (!consume('"'))
        {
            return fail("expected string");
        }
        out.clear();
        while (!atEnd())
        {
            const char c = text_[pos_++];
            if (c == '"')
            {
                return true;
            }
            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }
            if (atEnd())
            {
                return fail("truncated escape");
            }
            switch (const char esc = text_[pos_++])
            {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
            {
                std::uint32_t codePoint = 0;
                if (!parseHex4(codePoint))
                {
                    return false;
                }
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF && pos_ + 6 <= text_.size() &&
                    text_[pos_] == '\\' && text_[pos_ + 1] == 'u')
                {
                    pos_ += 2;
                    std::uint32_t low = 0;
                    if (!parseHex4(low))
                    {
                        return false;
                    }
                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                }
                appendUtf8(out, codePoint);
                break;
            }
            default:
                (void)esc;
                return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    std::optional<Value> parseArray()
    {
        consume('[');
        Value value;
        value.type = Value::Type::Array;
        skipWhitespace();
        if (consume(']'))
        {
            return value;
        }
        while (true)
        {
            auto element = parseValue();
            if (!element)
            {
                return std::nullopt;
            }
            value.array.push_back(std::move(*element));
            skipWhitespace();
            if (consume(','))
            {
                continue;
            }
            if (consume(']'))
            {
                return value;
            }
            fail("expected ',' or ']'");
            return std::nullopt;
        }
    }

    std::optional<Value> parseObject()
    {
        consume('{');
        Value value;
        value.type = Value::Type::Object;
        skipWhitespace();
        if (consume('}'))
        {
            return value;
        }
        while (true)
        {
            skipWhitespace();
            std::string key;
            if (!parseString(key))
            {
                return std::nullopt;
            }
            skipWhitespace();
            if (!consume(':'))
            {
                fail("expected ':'");
                return std::nullopt;
            }
            auto member = parseValue();
            if (!member)
            {
                return std::nullopt;
            }
            value.object.emplace_back(std::move(key), std::move(*member));
            skipWhitespace();
            if (consume(','))
            {
                continue;
            }
            if (consume('}'))
            {
                return value;
            }
            fail("expected ',' or '}'");
            return std::nullopt;
        }
    }
};

} // namespace

const Value* Value::find(std::string_view key) const
{
    if (type != Type::Object)
    {
        return nullptr;
    }
    for (const auto& [memberKey, memberValue] : object)
    {
        if (memberKey == key)
        {
            return &memberValue;
        }
    }
    return nullptr;
}

std::string Value::stringOr(std::string_view key, std::string fallback) const
{
    const Value* member = find(key);
    if (!member || member->type != Type::String)
    {
        return fallback;
    }
    return member->string;
}

long long Value::numberOr(std::string_view key, long long fallback) const
{
    const Value* member = find(key);
    if (!member || member->type != Type::Number)
    {
        return fallback;
    }
    return static_cast<long long>(std::llround(member->number));
}

bool Value::boolOr(std::string_view key, bool fallback) const
{
    const Value* member = find(key);
    if (!member || member->type != Type::Bool)
    {
        return fallback;
    }
    return member->boolean;
}

const std::vector<Value>* Value::arrayOr(std::string_view key) const
{
    const Value* member = find(key);
    if (!member || member->type != Type::Array)
    {
        return nullptr;
    }
    return &member->array;
}

std::optional<Value> parse(std::string_view text, std::string& error)
{
    Parser parser(text, error);
    return parser.run();
}

} // namespace cppsentinel::json
