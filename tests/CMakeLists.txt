# Unit and integration tests for CppSentinel (linked against cppsentinel_core).
#
# On MinGW both test binaries are linked statically, for the same reason as the
# CLI: a runner without MinGW on PATH must still be able to start them. The test
# PATH is derived from the compiler location - never hard-coded to C:\msys64,
# which is not where MSYS2 lives on a GitHub runner.

set(CPPSENTINEL_TEST_SOURCES
    unit/test_path_util.cpp
    unit/test_config_loader.cpp
    unit/test_diagnostic_parser.cpp
    unit/test_json_reporter.cpp
    unit/test_findings.cpp
    unit/test_filter.cpp
    unit/test_baseline.cpp
    unit/test_fix_parser.cpp
    unit/test_targets.cpp
    unit/test_cache.cpp
    unit/test_report_dir.cpp
    unit/test_engine_fake_runner.cpp
    unit/test_compile_commands.cpp
    integration/test_native_process.cpp
)

add_executable(cppsentinel_tests ${CPPSENTINEL_TEST_SOURCES})
target_link_libraries(cppsentinel_tests PRIVATE cppsentinel_core)
target_include_directories(cppsentinel_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if (MINGW)
    target_link_options(cppsentinel_tests PRIVATE -static)

    # <prefix>/mingw64/bin/<compiler> -> <prefix>/mingw64/bin, <prefix>/usr/bin
    get_filename_component(CPPSENTINEL_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_cppsentinel_msys2_prefix "${CPPSENTINEL_MINGW_BIN}" DIRECTORY)
    get_filename_component(_cppsentinel_msys2_prefix "${_cppsentinel_msys2_prefix}" DIRECTORY)
    set(CPPSENTINEL_TEST_PATH
        "${CPPSENTINEL_MINGW_BIN};${_cppsentinel_msys2_prefix}/usr/bin;$ENV{PATH}")
    unset(_cppsentinel_msys2_prefix)
else()
    set(CPPSENTINEL_TEST_PATH "$ENV{PATH}")
endif()

add_test(NAME cppsentinel_tests COMMAND cppsentinel_tests)
set_tests_properties(cppsentinel_tests PROPERTIES
    ENVIRONMENT "PATH=${CPPSENTINEL_TEST_PATH}"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Smoke: CLI help exits 0
add_test(NAME cppsentinel_cli_help COMMAND CppSentinel --help)
set_tests_properties(cppsentinel_cli_help PROPERTIES
    ENVIRONMENT "PATH=${CPPSENTINEL_TEST_PATH}"
)

# Smoke: binary starts without MinGW pre-seeded beyond the injected PATH above.
add_test(NAME cppsentinel_cli_missing_args COMMAND CppSentinel)
set_tests_properties(cppsentinel_cli_missing_args PROPERTIES
    ENVIRONMENT "PATH=${CPPSENTINEL_TEST_PATH}"
    WILL_FAIL TRUE
)
