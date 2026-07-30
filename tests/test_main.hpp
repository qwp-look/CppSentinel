#pragma once

// Minimal multi-TU test harness for cppsentinel_tests.
// Each test .cpp defines TEST(name) bodies; integration/test_native_process.cpp
// provides main() and calls runAll().

#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppsentinel::test
{

struct Registry
{
    static Registry& instance()
    {
        static Registry r;
        return r;
    }

    void add(std::string name, std::function<void()> fn)
    {
        cases.push_back({std::move(name), std::move(fn)});
    }

    struct Case
    {
        std::string name;
        std::function<void()> fn;
    };
    std::vector<Case> cases;
};

struct Registrar
{
    Registrar(std::string name, std::function<void()> fn)
    {
        Registry::instance().add(std::move(name), std::move(fn));
    }
};

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
    static ::cppsentinel::test::Registrar test_reg_##name{                      \
        #name, [] { test_fn_##name(); }};                                      \
    static void test_fn_##name()

#define REQUIRE(expr)                                                          \
    ::cppsentinel::test::require(static_cast<bool>(expr), #expr, __FILE__,     \
                                 __LINE__)
