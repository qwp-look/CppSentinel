#include "test_main.hpp"

#include "cppsentinel/util/Json.h"

#include <string>

using namespace cppsentinel;

TEST(json_parses_objects_arrays_and_scalars)
{
    const std::string text = R"({
  "version": 3,
  "enabled": true,
  "name": "escaped \"quote\" and \\ slash",
  "items": [1, 2.5, -3, null, {"inner": "x"}],
  "nested": {"a": {"b": 7}}
})";

    std::string error;
    auto root = json::parse(text, error);
    REQUIRE(root.has_value());
    REQUIRE(root->numberOr("version") == 3);
    REQUIRE(root->boolOr("enabled"));
    REQUIRE(root->stringOr("name") == "escaped \"quote\" and \\ slash");

    const auto* items = root->arrayOr("items");
    REQUIRE(items != nullptr);
    REQUIRE(items->size() == 5);
    REQUIRE((*items)[1].type == json::Value::Type::Number);
    REQUIRE((*items)[3].isNull());

    const auto* nested = root->find("nested");
    REQUIRE(nested != nullptr);
    const auto* inner = nested->find("a");
    REQUIRE(inner != nullptr);
    REQUIRE(inner->numberOr("b") == 7);
}

TEST(json_handles_unicode_escapes)
{
    std::string error;
    auto root = json::parse(R"({"msg":"caf\u00e9 \u4e2d\u6587"})", error);
    REQUIRE(root.has_value());
    REQUIRE(root->stringOr("msg") == "caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87");
}

TEST(json_reports_errors_without_crashing)
{
    std::string error;
    REQUIRE(!json::parse("{ broken", error).has_value());
    REQUIRE(!error.empty());

    error.clear();
    REQUIRE(!json::parse("[1, 2", error).has_value());

    error.clear();
    REQUIRE(!json::parse("{} trailing", error).has_value());
}

TEST(json_missing_fields_fall_back)
{
    std::string error;
    auto root = json::parse("{}", error);
    REQUIRE(root.has_value());
    REQUIRE(root->stringOr("absent", "fallback") == "fallback");
    REQUIRE(root->numberOr("absent", 42) == 42);
    REQUIRE(root->arrayOr("absent") == nullptr);
}
