cmake_minimum_required(VERSION 3.16)
project(CppSentinel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# 内嵌 Boost（bcp 输出位于 third_party/boost）
#   头文件 : third_party/boost/boost/...
#   源文件 : third_party/boost/libs/process/src/...
# 刷新方式 : scripts/vendor_boost.ps1 -BoostSource <boost_root>
# 文档     : docs/boost_vendor.zh-CN.md
# ---------------------------------------------------------------------------
set(CPPSENTINEL_BOOST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost")
if (NOT EXISTS "${CPPSENTINEL_BOOST_ROOT}/boost/process/v2/process.hpp")
    message(FATAL_ERROR
        "缺少内嵌的 Boost.Process 头文件（third_party/boost）。\n"
        "请执行: scripts/vendor_boost.ps1 -BoostSource <boost 源码根目录>")
endif()

set(CPPSENTINEL_PROCESS_SRC_DIR
    "${CPPSENTINEL_BOOST_ROOT}/libs/process/src")

# Boost.Process v2 翻译单元（参见 libs/process/CMakeLists.txt）。
# POSIX 专用 .cpp 仍列入列表；在 Windows 上由 BOOST_PROCESS_V2_POSIX 宏保护为空编译单元。
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

add_executable(CppSentinel
    main.cpp
    src/CommandBuilder.cpp
    src/Msys2Runner.cpp
    src/ReportDir.cpp
    src/Scheduler.cpp
    ${CPPSENTINEL_BOOST_PROCESS_SOURCES}
)

target_include_directories(CppSentinel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party          # toml.hpp
    ${CPPSENTINEL_BOOST_ROOT}                       # boost/...
)

target_compile_definitions(CppSentinel PRIVATE
    # Process v2 内部优先使用 std::filesystem，而非 Boost.Filesystem。
    BOOST_PROCESS_USE_STD_FS=1
    # 关闭 MSVC 自动链接 pragma；Process 源码由本工程自行编译。
    BOOST_PROCESS_NO_LIB=1
    BOOST_ALL_NO_LIB=1
    # 静态链接语义。
    BOOST_PROCESS_STATIC_LINK=1
)

# Process 源文件需要 BOOST_PROCESS_SOURCE，以保证 DECL 宏正确。
set_source_files_properties(
    ${CPPSENTINEL_BOOST_PROCESS_SOURCES}
    PROPERTIES
        COMPILE_DEFINITIONS "BOOST_PROCESS_SOURCE=1"
)

if (WIN32)
    # 与 Boost.Process 在 Windows 上的链接需求一致。
    target_link_libraries(CppSentinel PRIVATE
        ws2_32
        ntdll
        shell32
        advapi32
        user32
        bcrypt
    )
endif()
