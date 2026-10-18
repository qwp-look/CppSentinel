# CppSentinel v2 — configurable C++ audit orchestrator.
# Targets:
#   cppsentinel_core  static library (config/core/checker/process/report)
#   CppSentinel       CLI: run --config --target
#   cppsentinel_tests CTest binary (optional)
#
# Docs: README.md, docs/architecture.md, docs/configuration.md

cmake_minimum_required(VERSION 3.16)
project(CppSentinel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Needed by clang-tidy / compiler-diagnostics checkers (project.compilation_database).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CPPSENTINEL_BUILD_TESTS "Build CppSentinel unit/integration tests" ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BoostProcess.cmake")

# ---------------------------------------------------------------------------
# Core static library (all non-CLI logic)
# ---------------------------------------------------------------------------
add_library(cppsentinel_core STATIC
    src/util/FileIo.cpp
    src/util/Json.cpp
    src/process/PathUtil.cpp
    src/process/Environment.cpp
    src/process/ProcessLaunch.cpp
    src/process/NativeProcessRunner.cpp
    src/process/Msys2ProcessRunner.cpp
    src/process/FakeProcessRunner.cpp
    src/config/ConfigLoader.cpp
    src/report/Findings.cpp
    src/report/Filter.cpp
    src/report/Baseline.cpp
    src/report/SourceContext.cpp
    src/report/FixParser.cpp
    src/report/JsonReporter.cpp
    src/report/SarifReporter.cpp
    src/report/MarkdownReporter.cpp
    src/report/ConsoleReporter.cpp
    src/report/ReportWriter.cpp
    src/core/CheckerRegistry.cpp
    src/core/BuiltinTools.cpp
    src/core/ToolProbe.cpp
    src/core/Doctor.cpp
    src/core/Engine.cpp
    src/checker/DiagnosticParser.cpp
    src/checker/CompileCommands.cpp
    src/checker/BuiltinCheckers.cpp
    ${CPPSENTINEL_BOOST_PROCESS_SOURCES}
)

target_include_directories(cppsentinel_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

cppsentinel_configure_boost_process(cppsentinel_core)

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(CppSentinel main.cpp)
target_link_libraries(CppSentinel PRIVATE cppsentinel_core)

if (MINGW)
    # Prefer a fully-static MinGW runtime so the exe starts from plain PowerShell.
    target_link_options(CppSentinel PRIVATE -static)
endif()

include(GNUInstallDirs)
install(TARGETS CppSentinel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Fallback: if the linked binary still needs MinGW DLLs, install them beside it.
if (MINGW AND CMAKE_CXX_COMPILER)
    foreach(_dll IN ITEMS libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll)
        if (EXISTS "${CMAKE_CXX_COMPILER}/../${_dll}")
            install(FILES "${CMAKE_CXX_COMPILER}/../${_dll}"
                    DESTINATION ${CMAKE_INSTALL_BINDIR}
                    OPTIONAL)
        elseif (EXISTS "C:/msys64/mingw64/bin/${_dll}")
            install(FILES "C:/msys64/mingw64/bin/${_dll}"
                    DESTINATION ${CMAKE_INSTALL_BINDIR}
                    OPTIONAL)
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include(CTest)
if (CPPSENTINEL_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
