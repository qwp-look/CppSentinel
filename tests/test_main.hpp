#pragma once

// Minimal multi-TU test harness for cppsentinel_tests.
// Each test .cpp defines TEST(name) bodies; integration/test_native_process.cpp
// provides main() and calls runAll().

#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppsentinel::test
{

using CaseFn = void (*)();

struct Registry
{
    static Registry& instance()
    {
        static Registry r;
        return r;
    }

    struct Case
    {
        std::string name;
        CaseFn fn = nullptr;
    };
    std::vector<Case> cases;
};

/// Registration happens during dynamic initialisation, where a throwing
/// constructor terminates the process before main(). Test bodies never capture,
/// so a plain function pointer keeps every allocation inside this noexcept call
/// - which is also what makes the static initialiser provably non-throwing.
inline bool registerCase(const char* name, CaseFn fn) noexcept
{
    try
    {
        Registry::instance().cases.push_back({name, fn});
        return true;
    }
    catch (...)
    {
        return false;
    }
}

inline int runAll()
{
    int failed = 0;
    int total = 0;
    for (const auto& c : Registry::instance().cases)
    {
        ++total;
        try
        {
            c.fn();
            std::cout << "[ PASS ] " << c.name << '\n';
        }
        catch (const std::exception& ex)
        {
            ++failed;
            std::cout << "[ FAIL ] " << c.name << " : " << ex.what() << '\n';
        }
        catch (...)
        {
            ++failed;
            std::cout << "[ FAIL ] " << c.name << " : unknown exception\n";
        }
    }
    std::cout << "\n" << (total - failed) << "/" << total << " passed\n";
    return failed == 0 ? 0 : 1;
}

struct AssertFailure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Assert that an optional holds a value and hand back the value itself.
/// Tests then work with plain objects, so neither the test nor the analyzer has
/// to reason about a second, unchecked access.
template <typename T>
T requireValue(std::optional<T> value, const char* expr, const char* file,
               int line)
{
    if (!value)
    {
        throw AssertFailure(std::string(file) + ":" + std::to_string(line) +
                            " expected a value: " + expr);
    }
    return std::move(*value);
}

inline void require(bool cond, const char* expr, const char* file, int line)
{
    if (!cond)
    {
        throw AssertFailure(std::string(file) + ":" + std::to_string(line) +
                            " assert failed: " + expr);
    }
}

} // namespace cppsentinel::test

#define TEST(name)                                                             \
    static void test_fn_##name();                                              \
    [[maybe_unused]] static const bool test_reg_##name =                       \
        ::cppsentinel::test::registerCase(#name, &test_fn_##name);             \
    static void test_fn_##name()

#define REQUIRE(expr)                                                          \
    ::cppsentinel::test::require(static_cast<bool>(expr), #expr, __FILE__,     \
                                 __LINE__)

#define REQUIRE_VALUE(expr)                                                    \
    ::cppsentinel::test::requireValue((expr), #expr, __FILE__, __LINE__)
