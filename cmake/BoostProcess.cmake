# Vendored Boost.Process v2 (bcp output under third_party/boost).
#
# Used by cppsentinel_core for subprocess launch, env blocks, and timeouts.
# Refresh:
#   scripts/vendor_boost.ps1 -BoostSource <boost_root> [-Msys2Root <path>]
# Docs: docs/boost_vendor.md / docs/boost_vendor.zh-CN.md

set(CPPSENTINEL_BOOST_ROOT "${CMAKE_SOURCE_DIR}/third_party/boost")
if (NOT EXISTS "${CPPSENTINEL_BOOST_ROOT}/boost/process/v2/process.hpp")
    message(FATAL_ERROR
        "Missing vendored Boost.Process headers (third_party/boost).\n"
        "Run: scripts/vendor_boost.ps1 -BoostSource <boost source root>")
endif()

set(CPPSENTINEL_PROCESS_SRC_DIR
    "${CPPSENTINEL_BOOST_ROOT}/libs/process/src")

# Process v2 translation units (see libs/process/CMakeLists.txt).
# POSIX-only .cpp files remain listed; on Windows they compile empty under
# BOOST_PROCESS_V2_POSIX guards.
set(CPPSENTINEL_BOOST_PROCESS_SOURCES
    ${CPPSENTINEL_PROCESS_SRC_DIR}/detail/environment_posix.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/detail/environment_win.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/detail/last_error.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/detail/process_handle_windows.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/detail/throw_error.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/detail/utf8.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/ext/cmd.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/ext/cwd.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/ext/env.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/ext/exe.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/ext/proc_info.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/posix/close_handles.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/windows/default_launcher.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/environment.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/error.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/pid.cpp
    ${CPPSENTINEL_PROCESS_SRC_DIR}/shell.cpp
)

function(cppsentinel_configure_boost_process target_name)
    target_include_directories(${target_name} PUBLIC
        ${CPPSENTINEL_BOOST_ROOT}
    )
    target_compile_definitions(${target_name} PUBLIC
        BOOST_PROCESS_USE_STD_FS=1
        BOOST_PROCESS_NO_LIB=1
        BOOST_ALL_NO_LIB=1
        BOOST_PROCESS_STATIC_LINK=1
    )
    set_source_files_properties(
        ${CPPSENTINEL_BOOST_PROCESS_SOURCES}
        PROPERTIES
            COMPILE_DEFINITIONS "BOOST_PROCESS_SOURCE=1"
    )
    if (WIN32)
        target_link_libraries(${target_name} PUBLIC
            ws2_32
            ntdll
            shell32
            advapi32
            user32
            bcrypt
        )
    endif()
endfunction()
