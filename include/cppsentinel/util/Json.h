#pragma once

// Minimal read-only JSON DOM used for baseline and cache files.
// The project intentionally has no JSON dependency; report writing is done by
// hand (JsonReporter / SarifReporter) and reading needs only this small parser.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cppsentinel::json
{

struct Value
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    bool isNull() const noexcept
    {
        return type == Type::Null;
    }
    bool isArray() const noexcept
    {
        return type == Type::Array;
    }
    bool isObject() const noexcept
    {
        return type == Type::Object;
    }

    /// Object member lookup; nullptr when absent or not an object.
    const Value* find(std::string_view key) const;

    std::string stringOr(std::string_view key, std::string fallback = {}) const;
    long long numberOr(std::string_view key, long long fallback = 0) const;
    bool boolOr(std::string_view key, bool fallback = false) const;
    const std::vector<Value>* arrayOr(std::string_view key) const;
};

/// Parse a JSON document. Returns nullopt and fills `error` on failure.
std::optional<Value> parse(std::string_view text, std::string& error);

} // namespace cppsentinel::json
