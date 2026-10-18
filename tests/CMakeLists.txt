# Unit and integration tests for CppSentinel v2 (linked against cppsentinel_core).
# PATH is injected so native/MSYS2 tool lookups work under ctest on Windows.

set(CPPSENTINEL_TEST_SOURCES
    unit/test_path_util.cpp
    unit/test_config_loader.cpp
    unit/test_diagnostic_parser.cpp
    unit/test_json_reporter.cpp
    unit/test_findings.cpp
    unit/test_filter.cpp
    unit/test_baseline.cpp
    unit/test_json_parser.cpp
    unit/test_fix_parser.cpp
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

# Explicit MSYS2 tool PATH so tests can find gcc/bash when needed.
set(CPPSENTINEL_TEST_PATH
    "C:\\msys64\\mingw64\\bin;C:\\msys64\\usr\\bin;$ENV{PATH}")

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
