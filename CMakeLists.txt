cmake_minimum_required(VERSION 3.20)
project(mothprobe)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force static linking of runtime library (MSVC /MT or /MTd)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Configure spdlog to use external fmt
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt library" FORCE)
set(SODIUM_DISABLE_TESTS ON CACHE BOOL "Disable libsodium vendor tests" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "Disable cpp-httplib tests" FORCE)

# Add subdirectories
add_subdirectory(third_party/fmt)
add_subdirectory(third_party/spdlog)
add_subdirectory(third_party/json)
add_subdirectory(third_party/catch2)
add_subdirectory(third_party/tomlplusplus)
add_subdirectory(third_party/cpp-httplib)
add_subdirectory(third_party/libsodium)

add_library(mothprobe_project INTERFACE)
target_include_directories(mothprobe_project INTERFACE src)
target_compile_definitions(mothprobe_project INTERFACE SPDLOG_FMT_EXTERNAL SODIUM_STATIC=1)

set(MOTHPROBE_CORE_SOURCES
    src/core/audit.cpp
    src/core/config.cpp
    src/core/logger.cpp
    src/core/shell_runner.cpp
)

set(MOTHPROBE_MCP_SOURCES
    src/mcp/main_mcp.cpp
    src/mcp/json_rpc.cpp
    src/mcp/scope_validator.cpp
    src/mcp/server.cpp
)

add_executable(mothprobe_mcp ${MOTHPROBE_MCP_SOURCES} ${MOTHPROBE_CORE_SOURCES})
target_link_libraries(mothprobe_mcp PRIVATE
    mothprobe_project fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus httplib::httplib sodium
)
add_custom_command(TARGET mothprobe_mcp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/data/.mothprobe/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:mothprobe_mcp> ${CMAKE_SOURCE_DIR}/data/.mothprobe/bin/$<TARGET_FILE_NAME:mothprobe_mcp>
)

# Test Executable
enable_testing()
add_executable(smoke_test
    tests/test.cpp
)
target_link_libraries(smoke_test PRIVATE
    mothprobe_project
    Catch2::Catch2WithMain
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
    httplib::httplib
    sodium
)
add_dependencies(smoke_test mothprobe_mcp)
add_test(NAME SmokeTest COMMAND smoke_test)
